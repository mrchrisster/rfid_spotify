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

// ——— Log‐history for Telnet replay ———
static const size_t MAX_LOG_HISTORY = 50;
std::deque<String> logHistory;
WiFiServer telnetServer(23);
WiFiClient telnetClient;

void logMessage(const String& msg) { logHistory.push_back(msg); if (logHistory.size() > MAX_LOG_HISTORY) logHistory.pop_front(); Serial.println(msg); if (telnetClient && telnetClient.connected()) { telnetClient.println(msg); } }
#define LOG(x) logMessage(x)

// ——— NFC reader (using the safe pins for ESP32-C6) ———
#define RST_PIN 10
#define SS_PIN  11
#define SPI_SCK  1
#define SPI_MISO 6
#define SPI_MOSI 7
MFRC522 mfrc522(SS_PIN, RST_PIN);
NfcAdapter nfc = NfcAdapter(&mfrc522);

// ——— Spotify client ———
SpotifyClient spotify(clientId, clientSecret, deviceName, refreshToken);
static String currentDeviceId = "";

struct Album { String name; String uri; };

// ——— Forward declarations ———
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
    currentDeviceId = spotify.GetDevices();
    LOG("[Main] Stored Device ID: " + currentDeviceId);
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
    currentDeviceId = "";
    wasDisconnected = false;
  }
  readNFCTag();
  delay(500);
}

// ——— Telnet & Wi-Fi helpers ———
void handleTelnet() { if (telnetServer.hasClient()) { if (!telnetClient || !telnetClient.connected()) { telnetClient = telnetServer.available(); telnetClient.flush(); for (auto &line : logHistory) { telnetClient.println(line); } LOG("[Telnet] New client connected"); } else { WiFiClient busy = telnetServer.available(); busy.println("Busy – one client only"); busy.stop(); } } }
void connectWifi() { LOG("[Main] Connecting to Wi-Fi..."); WiFi.begin(ssid, pass); unsigned long start = millis(); while (WiFi.status() != WL_CONNECTED && millis() - start < 30000) { delay(500); Serial.print('.'); } if (WiFi.status() == WL_CONNECTED) { LOG("\n[Main] Wi-Fi connected: " + WiFi.localIP().toString()); } else { LOG("\n[Main] Wi-Fi FAILED"); } }
void ensureWifiConnected() { static unsigned long lastTry = 0; if (WiFi.status() == WL_CONNECTED) return; unsigned long now = millis(); if (now - lastTry > 5000) { LOG("[Main] Wi-Fi lost – retrying"); WiFi.disconnect(); WiFi.begin(ssid, pass); lastTry = now; } }
void logError(const String& msg, int code) { LOG("[Error] " + msg + " (HTTP " + String(code) + ")"); }

// ——— NFC tag & NDEF reading ———
void readNFCTag() {
    if (!nfc.tagPresent()) { return; }
    NfcTag tag = nfc.read();
    LOG("[Main] Tag detected! UID: " + tag.getUidString());
    if (!tag.hasNdefMessage()) { LOG("[NFC] Tag is not NDEF formatted."); delay(2000); return; }
    NdefMessage message = tag.getNdefMessage();
    String finalUri = "";
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
            String rawUri = String(text);
            LOG("[NFC] Found Text Record: " + rawUri);

            String spotifyUri = rawUri;
            int questionMarkIndex = spotifyUri.indexOf('?');
            if (questionMarkIndex != -1) {
                spotifyUri = spotifyUri.substring(0, questionMarkIndex);
            }
            if (spotifyUri.startsWith("https://open.spotify.com/")) {
                spotifyUri.replace("https://open.spotify.com/", "spotify:");
                int slashIndex = spotifyUri.indexOf('/');
                if (slashIndex != -1) {
                    spotifyUri.setCharAt(slashIndex, ':');
                }
            }
            if (spotifyUri.startsWith("spotify:")) { finalUri = spotifyUri; break; }
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
        LOG("[Main] No valid Spotify URI or URL found on this card.");
    }
    delay(3000);
}

// ——— Spotify playback helpers ———
void playSpotifyUri(const String& uri) {
  LOG("[Main] playSpotifyUri→ " + uri);
  disableShuffle();
  for (int attempt = 1; attempt <= 3; attempt++) {
    int code = spotify.Play(uri);
    if (code == 200 || code == 204) { LOG("[Main] Playback OK"); return; }
    if (code == 404) { LOG("[Main] Device not found (404). Clearing stored ID to force re-fetch on next play."); currentDeviceId = ""; }
    if (code == 401 || code == 404) { LOG("[Main] Resetting state (err " + String(code) + ")"); spotify.ResetState();
    } else { logError("playSpotifyUri", code); }
    delay(2000);
  }
  LOG("[Main] Giving up on playSpotifyUri");
}

void disableShuffle() {
  spotify.EnsureTokenFresh();
  if (currentDeviceId.length() == 0) {
    LOG("[Main] No stored Device ID. Fetching device list...");
    currentDeviceId = spotify.GetDevices();
    LOG("[Main] Stored new Device ID: " + currentDeviceId);
  }
  if (currentDeviceId.isEmpty()) { LOG("[Main] No active device found for disableShuffle"); return; }
  String url = "https://api.spotify.com/v1/me/player/shuffle?state=false&device_id=" + currentDeviceId;
  HttpResult r = spotify.CallAPI("PUT", url, "{}");
  if (r.httpCode == 200 || r.httpCode == 204) { LOG("[Main] Shuffle OFF"); } 
  else { logError("disableShuffle", r.httpCode); }
}

void playRandomAlbumFromArtist(const String& artistUri) {
  // Static variables to remember the current playlist between function calls
  static String lastArtistId;
  static std::vector<int> albumPlaylistIndices;
  static size_t playlistIndex = 0;
  String artistId = artistUri.substring(15);

  // If the artist is new, build a shuffled list of their album INDICES
  if (artistId != lastArtistId) {
    LOG("[Main] New artist detected. Building shuffled index for " + artistId);
    albumPlaylistIndices.clear();

    // Step 1: Get the total count of albums from the API
    String countUrl = "https://api.spotify.com/v1/artists/" + artistId + "/albums?include_groups=album,single&limit=1";
    HttpResult countResult = spotify.CallAPI("GET", countUrl, "");
    if (countResult.httpCode != 200) {
      logError("fetch album count", countResult.httpCode);
      return;
    }
    DynamicJsonDocument countDoc(1024);
    deserializeJson(countDoc, countResult.payload);
    int totalAlbums = countDoc["total"];

    if (totalAlbums == 0) {
      LOG("[Main] No albums found for this artist.");
      return;
    }

    // Step 2: Determine the size and range of our playlist
    int playlistSize = totalAlbums;
    int startOffset = 0; // The index to start our list from

    if (artistId == "1l6d0RIxTL3JytlLGvWzYe" && totalAlbums > 60) {
      LOG("[Main] Special artist: Creating playlist from the 60 oldest albums.");
      playlistSize = 60;
      // CORRECTED: Calculate the starting index of the oldest albums
      startOffset = totalAlbums - 60;
    } else {
      LOG("[Main] Building playlist with all " + String(playlistSize) + " album indices.");
    }

    // Step 3: Create and shuffle the list of indices
    albumPlaylistIndices.resize(playlistSize);
    for (int i = 0; i < playlistSize; i++) {
      // Fill the list with the correct range of indices (e.g., 169, 170, 171...)
      albumPlaylistIndices[i] = startOffset + i;
    }
    
    randomSeed(micros());
    for (int i = albumPlaylistIndices.size() - 1; i > 0; --i) {
      int j = random(0, i + 1);
      std::swap(albumPlaylistIndices[i], albumPlaylistIndices[j]);
    }
    
    playlistIndex = 0;
    lastArtistId = artistId;
    LOG("[Main] Shuffled index created successfully.");
  }

  // Check if we've played everything. If so, reshuffle the index.
  if (playlistIndex >= albumPlaylistIndices.size()) {
    LOG("[Main] Playlist exhausted. Reshuffling index...");
    randomSeed(micros());
    for (int i = albumPlaylistIndices.size() - 1; i > 0; --i) {
      int j = random(0, i + 1);
      std::swap(albumPlaylistIndices[i], albumPlaylistIndices[j]);
    }
    playlistIndex = 0;
  }

  // Step 4: Get the next album index from our shuffled list and fetch ONLY that album
  int randomOffset = albumPlaylistIndices[playlistIndex];
  playlistIndex++;
  
  LOG("[Main] Playing album at index #" + String(randomOffset) + " (track " + String(playlistIndex) + " of " + String(albumPlaylistIndices.size()) + ")");

  String albumUrl = "https://api.spotify.com/v1/artists/" + artistId + "/albums?include_groups=album,single&limit=1&offset=" + String(randomOffset);
  HttpResult albumResult = spotify.CallAPI("GET", albumUrl, "");
  if (albumResult.httpCode != 200) {
    logError("fetch random album", albumResult.httpCode);
    return;
  }

  // This JSON is for a single album, so it's very small and safe to parse.
  DynamicJsonDocument albumDoc(2048);
  DeserializationError error = deserializeJson(albumDoc, albumResult.payload);
  if (error) {
    LOG("[Error] Failed to parse single album JSON: " + String(error.c_str()));
    return;
  }
  
  String albumUri = albumDoc["items"][0]["uri"];
  String albumName = albumDoc["items"][0]["name"];

  if (albumUri.length() > 0) {
    LOG("[Main] Now playing: " + albumName);
    playSpotifyUri(albumUri);
  } else {
    LOG("[Main] Failed to extract URI from single album data.");
  }
}
