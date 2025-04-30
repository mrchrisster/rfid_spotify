#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <vector>
#include <deque>
#include <algorithm>      // for std::swap
#include <ArduinoJson.h>
#include "MFRC522.h"
#include "SpotifyClient.h"
#include "settings.h"     // your ssid, pass, clientId, clientSecret, deviceName, refreshToken

// ——— Log‐history for Telnet replay ———
static const size_t MAX_LOG_HISTORY = 50;
std::deque<String> logHistory;

WiFiServer telnetServer(23);
WiFiClient telnetClient;

void logMessage(const String& msg) {
  // push into history
  logHistory.push_back(msg);
  if (logHistory.size() > MAX_LOG_HISTORY) logHistory.pop_front();
  // always Serial
  Serial.println(msg);
  // Telnet if connected
  if (telnetClient && telnetClient.connected()) {
    telnetClient.println(msg);
  }
}
#define LOG(x) logMessage(x)

// ——— NFC reader ———
#define RST_PIN 4
#define SS_PIN  5
MFRC522 mfrc522(SS_PIN, RST_PIN);

// ——— Spotify client ———
SpotifyClient spotify(clientId, clientSecret, deviceName, refreshToken);

// Simple struct for name+URI
struct Album {
  String name;
  String uri;
};

// ——— Forward declarations ———
void handleTelnet();
void connectWifi();
void ensureWifiConnected();
void logError(const String& msg, int code);
void readNFCTag();
String readFromCard();
MFRC522::StatusCode authenticateBlock(byte block, MFRC522::MIFARE_Key* key);
void playSpotifyUri(const String& uri);
void disableShuffle();
void playRandomAlbumFromArtist(const String& artistUri);

void setup() {
  Serial.begin(115200);
  LOG("[Main] Setup started");

  connectWifi();
  telnetServer.begin();
  telnetServer.setNoDelay(true);
  LOG("[Telnet] Server started on port 23");

  SPI.begin();
  mfrc522.PCD_Init();
  LOG("[Main] MFRC522 ready");

  // Prime Spotify token & deviceId
  if (!spotify.EnsureTokenFresh()) {
    LOG("[Main] WARNING: initial token fetch failed");
  } else {
    spotify.GetDevices();
  }
}

void loop() {
  handleTelnet();
  ensureWifiConnected();

  static bool wasDisconnected = false;
  if (WiFi.status() != WL_CONNECTED) {
    wasDisconnected = true;
  } else if (wasDisconnected) {
    LOG("[Main] Wi-Fi reconnected → resetting Spotify client");
    spotify.ResetState();
    wasDisconnected = false;
  }

  // Every time a tag is present, read & play
  if (mfrc522.PICC_IsNewCardPresent()) {
    LOG("[Main] NFC tag detected");
    readNFCTag();
  }
}

// ——— Telnet & Wi-Fi helpers ———

void handleTelnet() {
  if (telnetServer.hasClient()) {
    if (!telnetClient || !telnetClient.connected()) {
      telnetClient = telnetServer.available();
      telnetClient.flush();
      // replay history
      for (auto &line : logHistory) {
        telnetClient.println(line);
      }
      LOG("[Telnet] New client connected");
    } else {
      WiFiClient busy = telnetServer.available();
      busy.println("Busy – one client only");
      busy.stop();
    }
  }
}

void connectWifi() {
  LOG("[Main] Connecting to Wi-Fi…");
  WiFi.begin(ssid, pass);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 30000) {
    delay(500);
    Serial.print('.');
  }
  if (WiFi.status() == WL_CONNECTED) {
    LOG("[Main] Wi-Fi connected: " + WiFi.localIP().toString());
  } else {
    LOG("[Main] Wi-Fi FAILED");
  }
}

void ensureWifiConnected() {
  static unsigned long lastTry = 0;
  if (WiFi.status() == WL_CONNECTED) return;
  unsigned long now = millis();
  if (now - lastTry > 5000) {
    LOG("[Main] Wi-Fi lost – retrying");
    WiFi.disconnect();
    WiFi.begin(ssid, pass);
    lastTry = now;
  }
}

void logError(const String& msg, int code) {
  LOG("[Error] " + msg + " (HTTP " + String(code) + ")");
}

// ——— NFC tag & card reading ———

void readNFCTag() {
  if (!mfrc522.PICC_ReadCardSerial()) {
    LOG("[Main] Failed to read card serial");
    return;
  }

  String uri = readFromCard();
  LOG("[Main] Card URI: " + uri);

  // Clean up
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();

  if (uri.startsWith("spotify:artist:")) {
    playRandomAlbumFromArtist(uri);
  } else if (uri.startsWith("spotify:")) {
    playSpotifyUri(uri);
  } else {
    LOG("[Main] Unknown URI format");
  }
}

String readFromCard() {
  MFRC522::MIFARE_Key key;
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

  String result;
  byte block = 4;
  bool gotData = false;

  while (block < 64) {
    if (block % 4 == 3) { block++; continue; } // skip trailers

    if (authenticateBlock(block, &key) != MFRC522::STATUS_OK) break;

    byte buffer[18];
    byte size = sizeof(buffer);
    if (mfrc522.MIFARE_Read(block, buffer, &size) != MFRC522::STATUS_OK) break;

    String chunk;
    for (byte i = 0; i < 16; i++) {
      if (buffer[i] != 0) { chunk += (char)buffer[i]; gotData = true; }
    }
    if (!gotData) break;
    result += chunk;
    block++;
    gotData = false;
  }

  if (!result.startsWith("spotify:")) result = "spotify:" + result;
  return result;
}

MFRC522::StatusCode authenticateBlock(byte block, MFRC522::MIFARE_Key* key) {
  for (int i = 0; i < 3; i++) {
    auto st = mfrc522.PCD_Authenticate(
      MFRC522::PICC_CMD_MF_AUTH_KEY_A,
      block, key, &mfrc522.uid
    );
    if (st == MFRC522::STATUS_OK) return st;
  }
  return MFRC522::STATUS_ERROR;
}

// ——— Spotify playback helpers ———

void playSpotifyUri(const String& uri) {
  LOG("[Main] playSpotifyUri → " + uri);
  disableShuffle();
  for (int attempt = 1; attempt <= 3; attempt++) {
    int code = spotify.Play(uri);
    if (code == 200 || code == 204) {
      LOG("[Main] Playback OK");
      return;
    }
    if (code == 401 || code == 404) {
      LOG("[Main] Resetting state (err " + String(code) + ")");
      spotify.ResetState();
    } else {
      logError("playSpotifyUri", code);
    }
    delay(2000);
  }
  LOG("[Main] Giving up on playSpotifyUri");
}

void disableShuffle() {
  // ensure token and device
  spotify.EnsureTokenFresh();
  String dev = spotify.GetDevices();  // returns and sets deviceId
  if (dev.isEmpty()) {
    LOG("[Main] No device for disableShuffle");
    return;
  }
  String url = "https://api.spotify.com/v1/me/player/shuffle?state=false"
               "&device_id=" + dev;
  HttpResult r = spotify.CallAPI("PUT", url, "{}");
  if (r.httpCode == 200 || r.httpCode == 204) {
    LOG("[Main] Shuffle OFF");
  } else {
    logError("disableShuffle", r.httpCode);
  }
}

// ——— True no-repeat shuffle per artist ———

void playRandomAlbumFromArtist(const String& artistUri) {
  static String lastArtistId;
  static std::vector<Album> albumList;
  static std::vector<Album> playlist;
  static size_t idx = 0;

  // strip prefix
  String artistId = artistUri.substring(15);

  // rebuild on new artist
  if (artistId != lastArtistId) {
    LOG("[Main] Fetching all albums for " + artistId);
    albumList.clear();

    const int pageSize = 20;
    int offset = 0, total = 0;
    do {
      String url = "https://api.spotify.com/v1/artists/" + artistId +
                   "/albums?include_groups=album"
                   + "&limit="  + String(pageSize)
                   + "&offset=" + String(offset);
      HttpResult r = spotify.CallAPI("GET", url, "");
      if (r.httpCode != 200) {
        logError("fetch albums", r.httpCode);
        return;
      }
      DynamicJsonDocument doc(8192);
      auto err = deserializeJson(doc, r.payload);
      if (err) {
        logError("parse albums JSON", r.httpCode);
        return;
      }
      JsonArray items = doc["items"].as<JsonArray>();
      for (JsonObject a : items) {
        Album alb;
        alb.name = a["name"].as<String>();
        alb.uri  = a["uri"].as<String>();
        albumList.push_back(alb);
      }
      total  = doc["total"].as<int>();
      offset += pageSize;
    } while (offset < total);

    // print compiled list
    LOG("[Main] Compiled album list:");
    for (size_t i = 0; i < albumList.size(); i++) {
      LOG("  [" + String(i+1) + "] " + albumList[i].name);
    }

    // shuffle
    playlist = albumList;
    randomSeed(micros());
    for (int i = (int)playlist.size() - 1; i > 0; --i) {
      int j = random(0, i + 1);
      std::swap(playlist[i], playlist[j]);
    }
    idx = 0;
    lastArtistId = artistId;
    LOG("[Main] Built shuffled playlist of " + String(playlist.size()) + " albums");
  }

  // reshuffle if exhausted
  if (idx >= playlist.size()) {
    LOG("[Main] Playlist exhausted → reshuffling");
    randomSeed(micros());
    for (int i = (int)playlist.size() - 1; i > 0; --i) {
      int j = random(0, i + 1);
      std::swap(playlist[i], playlist[j]);
    }
    idx = 0;
  }

  // play next
  Album next = playlist[idx++];
  LOG("[Main] Playing album: " + next.name);
  disableShuffle();
  playSpotifyUri(next.uri);
}
