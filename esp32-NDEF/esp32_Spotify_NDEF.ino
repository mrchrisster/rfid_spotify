#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <vector>
#include <deque>
#include <algorithm>
#include <ArduinoJson.h>
#include "MFRC522.h"
#include "NfcAdapter.h"
#include "SpotifyClient.h"
#include "settings.h"

// Telnet logging integration
WiFiServer telnetServer(23);
WiFiClient telnetClient;

#define LOG(msg) do {                                   \
    Serial.println(msg);                                \
    if (telnetClient && telnetClient.connected())       \
      telnetClient.println(msg);                        \
  } while(0)

// NFC reader pins (using the safe pins for ESP32-C6)
#define RST_PIN 10
#define SS_PIN  11
#define SPI_SCK  1
#define SPI_MISO 6
#define SPI_MOSI 7

MFRC522 mfrc522(SS_PIN, RST_PIN);
NfcAdapter nfc = NfcAdapter(&mfrc522);

// Spotify client
SpotifyClient spotify(clientId, clientSecret, deviceName, refreshToken);
struct Album { String name; String uri; };

// Forward declarations
void handleTelnet();
void connectWifi();
void ensureWifiConnected();
void logError(const String& msg, int code);
void readNFCTag();
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

  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  mfrc522.PCD_Init();
  nfc.begin();
  LOG("[Main] MFRC522 and NDEF Reader ready");

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

  readNFCTag();
  delay(500);
}

// ————— Telnet & Wi-Fi helpers —————
void handleTelnet() { if (telnetServer.hasClient()) { if (!telnetClient || !telnetClient.connected()) { telnetClient = telnetServer.available(); telnetClient.flush(); LOG("[Telnet] New client connected"); } else { WiFiClient busy = telnetServer.available(); busy.println("Busy – one client only"); busy.stop(); } } }
void connectWifi() { LOG("[Main] Connecting to Wi-Fi..."); WiFi.begin(ssid, pass); unsigned long start = millis(); while (WiFi.status() != WL_CONNECTED && millis() - start < 30000) { delay(500); Serial.print('.'); } if (WiFi.status() == WL_CONNECTED) { LOG("\n[Main] Wi-Fi connected: " + WiFi.localIP().toString()); } else { LOG("\n[Main] Wi-Fi FAILED"); } }
void ensureWifiConnected() { static unsigned long lastTry = 0; if (WiFi.status() == WL_CONNECTED) return; unsigned long now = millis(); if (now - lastTry > 5000) { LOG("[Main] Wi-Fi lost – retrying"); WiFi.disconnect(); WiFi.begin(ssid, pass); lastTry = now; } }
void logError(const String& msg, int code) { LOG("[Error] " + msg + " (HTTP " + String(code) + ")"); }


// ————— NFC tag & NDEF reading (URL PROCESSING LOGIC) —————
void readNFCTag() {
    if (!nfc.tagPresent()) {
        return; // No card, do nothing
    }

    NfcTag tag = nfc.read();
    LOG("[Main] Tag detected! UID: " + tag.getUidString());

    if (!tag.hasNdefMessage()) {
        LOG("[NFC] Tag is not NDEF formatted.");
        delay(2000);
        return;
    }

    NdefMessage message = tag.getNdefMessage();
    String finalUri = ""; // This will hold the final spotify: URI

    // Loop through records to find the first valid Spotify URL
    for (int i = 0; i < message.getRecordCount(); i++) {
        NdefRecord record = message.getRecord(i);

        if (record.getTnf() == NdefRecord::TNF_WELL_KNOWN && record.getTypeLength() == 1 && record.getType()[0] == 'T') {
            int payloadLength = record.getPayloadLength();
            const byte* payload = record.getPayload();

            int langCodeLength = payload[0] & 0x3F;
            int textLength = payloadLength - (1 + langCodeLength);
            char text[textLength + 1];
            memcpy(text, &payload[1 + langCodeLength], textLength);
            text[textLength] = '\0';

            String rawUrl = String(text);
            LOG("[NFC] Found Text Record: " + rawUrl);

            if (rawUrl.startsWith("https://open.spotify.com/")) {
                LOG("[NFC] Valid Spotify URL found. Converting to URI for playback...");

                // Example: https://open.spotify.com/album/6pvHJgEqLmqmts4uBwMLdd
                int baseLen = String("https://open.spotify.com/").length();
                String path = rawUrl.substring(baseLen); // e.g., "album/6pvHJgEqLmqmts4uBwMLdd"

                path.replace("/", ":");                  // "album:6pvHJgEqLmqmts4uBwMLdd"
                String convertedUri = "spotify:" + path; // "spotify:album:6pvHJgEqLmqmts4uBwMLdd"

                LOG("[NFC] Converted URI: " + convertedUri);
                finalUri = convertedUri;
                break;
            }

        }
    }

    if (finalUri.length() > 0) {
        LOG("[Main] URI ready for playback: " + finalUri);
        if (finalUri.startsWith("spotify:artist:")) {
            playRandomAlbumFromArtist(finalUri);
        } else {
            playSpotifyUri(finalUri);
        }
    } else {
        LOG("[Main] No valid Spotify URL found on this card.");
    }
    
    // Give a little time before the next scan to prevent instant re-reads
    delay(3000);
}


// ————— Spotify playback helpers —————
void playSpotifyUri(const String& uri) {
  LOG("[Main] playSpotifyUri→ " + uri);
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
  auto r = spotify.CallAPI("PUT", "https://api.spotify.com/v1/me/player/shuffle?state=false", "{}");
  if (r.httpCode==200||r.httpCode==204) {
    LOG("[Main] Shuffle OFF");
  } else {
    logError("disableShuffle", r.httpCode);
  }
}

void playRandomAlbumFromArtist(const String& artistUri) {
  static String lastArtistId;
  static std::vector<String> albumUris;
  static std::vector<String> playlist;
  static size_t idx = 0;

  String artistId = artistUri.substring(15);

  if (artistId != lastArtistId) {
    LOG("[Main] Fetching all albums for " + artistId);
    albumUris.clear();
    const int limit = 50;
    int offset = 0, total = 0;

    do {
      String url = "https://api.spotify.com/v1/artists/" + artistId + "/albums?include_groups=album,single&limit=" + String(limit) + "&offset=" + String(offset);
      HttpResult r = spotify.CallAPI("GET", url, "");
      if (r.httpCode != 200) {
        logError("fetch albums", r.httpCode);
        return;
      }

      DynamicJsonDocument doc(16384);
      auto err = deserializeJson(doc, r.payload);
      if (err) {
        logError("parse albums JSON", r.httpCode);
        return;
      }

      JsonArray items = doc["items"].as<JsonArray>();
      for (JsonObject a : items) {
        albumUris.push_back(a["uri"].as<String>());
      }
      total  = doc["total"].as<int>();
      offset += limit;
    } while (offset < total);

    if (albumUris.empty()) {
      LOG("[Main] No albums found.");
      return;
    }

    playlist = albumUris;
    randomSeed(micros());
    for (int i = playlist.size() - 1; i > 0; --i) {
      int j = random(0, i + 1);
      std::swap(playlist[i], playlist[j]);
    }
    idx = 0;
    lastArtistId = artistId;
    LOG("[Main] Built shuffled playlist of " + String(playlist.size()) + " albums");
  }

  if (idx >= playlist.size()) {
    LOG("[Main] Playlist exhausted → reshuffling");
    randomSeed(micros());
    for (int i = playlist.size() - 1; i > 0; --i) {
      int j = random(0, i + 1);
      std::swap(playlist[i], playlist[j]);
    }
    idx = 0;
  }

  String albumUri = playlist[idx++];
  LOG("[Main] Playing album: " + albumUri);
  disableShuffle();
  playSpotifyUri(albumUri);
}
