#include <Arduino.h>
#include <Adafruit_ILI9341.h>
#include <JPEGDecoder.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <SPI.h>
#include <vector>
#include <deque>
#include <algorithm>
#include <ArduinoJson.h>
#include "MFRC522.h"
#include "NfcAdapter.h"      // Added for NDEF support
#include "SpotifyClient.h"
#include "settings.h"

#define MAX_JPEG   (64 * 1024)
static uint8_t jpgBuf[MAX_JPEG];

// --- Screen Definitions ---
#define TFT_CS    15
#define TFT_DC     2
#define TFT_RST   22
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
static uint32_t lastArtMillis = 0;

// --- Log‐history for Telnet replay ---
static const size_t MAX_LOG_HISTORY = 50;
std::deque<String> logHistory;
WiFiServer telnetServer(23);
WiFiClient telnetClient;
void logMessage(const String& msg) { logHistory.push_back(msg); if (logHistory.size() > MAX_LOG_HISTORY) logHistory.pop_front(); Serial.println(msg); if (telnetClient && telnetClient.connected()) { telnetClient.println(msg); } }
#define LOG(x) logMessage(x)

// --- NFC reader (using the correct pins for your device) ---
#define RST_PIN 4
#define SS_PIN  5
MFRC522 mfrc522(SS_PIN, RST_PIN);
NfcAdapter nfc = NfcAdapter(&mfrc522); // NDEF adapter object

// --- Spotify client ---
SpotifyClient spotify(clientId, clientSecret, deviceName, refreshToken);
static String currentDeviceId = "";

// --- Forward declarations ---
void handleTelnet();
void connectWifi();
void ensureWifiConnected();
void logError(const String& msg, int code);
void readNFCTag();
void playSpotifyUri(const String& uri);
void disableShuffle();
void playRandomAlbumFromArtist(const String& artistUri);
void showAlbumArt();
void renderJPEG(int xPos, int yPos);

void setup() {
  Serial.begin(115200);
  LOG("[Main] Setup started");

  SPI.begin(18, 19, 23); // Correct SPI pins for your device
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);

  connectWifi();
  telnetServer.begin();
  telnetServer.setNoDelay(true);
  LOG("[Telnet] Server started on port 23");

  mfrc522.PCD_Init();
  nfc.begin(); // Initialize the NDEF adapter
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
    currentDeviceId = ""; // Force re-fetch of device ID after reconnect
    wasDisconnected = false;
  }

  readNFCTag(); // Replaced the old check with the new function call

  // 30-minute idle → clear screen once
  if (lastArtMillis && millis() - lastArtMillis > 30UL * 60UL * 1000UL) {
    tft.fillScreen(ILI9341_BLACK);
    lastArtMillis = 0;
    LOG("[Main] screen cleared after 30 min");
  }

  delay(500); // Added delay to prevent a tight loop
}

// --- Telnet & Wi-Fi helpers (Unchanged) ---
void handleTelnet() { if (telnetServer.hasClient()) { if (!telnetClient || !telnetClient.connected()) { telnetClient = telnetServer.available(); telnetClient.flush(); for (auto &line : logHistory) { telnetClient.println(line); } LOG("[Telnet] New client connected"); } else { WiFiClient busy = telnetServer.available(); busy.println("Busy – one client only"); busy.stop(); } } }
void connectWifi() { LOG("[Main] Connecting to Wi-Fi..."); WiFi.begin(ssid, pass); unsigned long start = millis(); while (WiFi.status() != WL_CONNECTED && millis() - start < 30000) { delay(500); Serial.print('.'); } if (WiFi.status() == WL_CONNECTED) { LOG("\n[Main] Wi-Fi connected: " + WiFi.localIP().toString()); } else { LOG("\n[Main] Wi-Fi FAILED"); } }
void ensureWifiConnected() { static unsigned long lastTry = 0; if (WiFi.status() == WL_CONNECTED) return; unsigned long now = millis(); if (now - lastTry > 5000) { LOG("[Main] Wi-Fi lost – retrying"); WiFi.disconnect(); WiFi.begin(ssid, pass); lastTry = now; } }
void logError(const String& msg, int code) { LOG("[Error] " + msg + " (HTTP " + String(code) + ")"); }


// --- NEW: NDEF Tag Reading Logic ---
// This function completely replaces the old readNFCTag, readFromCard, and authenticateBlock functions.
void readNFCTag() {
    if (!nfc.tagPresent()) { return; }
    NfcTag tag = nfc.read();
    LOG("[Main] Tag detected! UID: " + tag.getUidString());
    if (!tag.hasNdefMessage()) { LOG("[NFC] Tag is not NDEF formatted."); delay(2000); return; }
    
    NdefMessage message = tag.getNdefMessage();
    String finalUri = "";

    for (int i = 0; i < message.getRecordCount(); i++) {
        NdefRecord record = message.getRecord(i);
        // We only care about Text records
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

            // Clean the URI to get a standard spotify: format
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
    delay(3000); // Wait a few seconds before allowing another scan
}


// --- Spotify playback helpers ---
void playSpotifyUri(const String& uri) {
  LOG("[Main] playSpotifyUri → " + uri);
  disableShuffle();
  for (int attempt = 1; attempt <= 3; attempt++) {
    int code = spotify.Play(uri);
    if (code == 200 || code == 204) {
      LOG("[Main] Playback OK");
      showAlbumArt();
      return;
    }
    if (code == 404) { LOG("[Main] Device not found (404). Clearing stored ID to force re-fetch on next play."); currentDeviceId = ""; }
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

// In your main .ino file, replace the existing function with this one.

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
    
    // --- THIS IS THE SPECIAL CASE LOGIC THAT WAS MISSING ---
    int playlistSize = totalAlbums;
    int startOffset = 0;
    // Check for the specific artist IDs and adjust the playlist size and offset
    if ((artistId == "1l6d0RIxTL3JytlLGvWzYe" || artistId == "3t2iKODSDyzoDJw7AsD99u") && totalAlbums > 60) {
      LOG("[Main] Special artist: Creating playlist from the 60 oldest albums.");
      playlistSize = 60;
      startOffset = totalAlbums - 60; // Start from the older albums
    } else {
      LOG("[Main] Building playlist with all " + String(playlistSize) + " album indices.");
    }

    albumPlaylistIndices.resize(playlistSize);
    for (int i = 0; i < playlistSize; i++) {
      albumPlaylistIndices[i] = startOffset + i;
    }
    // --- END OF SPECIAL CASE LOGIC ---
    
    randomSeed(micros());
    for (int i = albumPlaylistIndices.size() - 1; i > 0; --i) {
      int j = random(0, i + 1);
      std::swap(albumPlaylistIndices[i], albumPlaylistIndices[j]);
    }
    
    playlistIndex = 0;
    lastArtistId = artistId;
    LOG("[Main] Shuffled index created successfully.");
  }

  if (playlistIndex >= albumPlaylistIndices.size()) {
    LOG("[Main] Playlist exhausted. Reshuffling index...");
    randomSeed(micros());
    for (int i = albumPlaylistIndices.size() - 1; i > 0; --i) {
      int j = random(0, i + 1);
      std::swap(albumPlaylistIndices[i], albumPlaylistIndices[j]);
    }
    playlistIndex = 0;
  }

  String albumUri = "";
  String albumName = "";
  
  // Get the album URI from the pre-shuffled list
  int randomOffset = albumPlaylistIndices[playlistIndex];
  playlistIndex++;
  
  LOG("[Main] Playing album at index #" + String(randomOffset) + " (track " + String(playlistIndex) + " of " + String(albumPlaylistIndices.size()) + ")");
  String albumUrl = "https://api.spotify.com/v1/artists/" + artistId + "/albums?include_groups=album,single&limit=1&offset=" + String(randomOffset);
  HttpResult albumResult = spotify.CallAPI("GET", albumUrl, "");
  if (albumResult.httpCode == 200) {
    DynamicJsonDocument albumDoc(2048);
    deserializeJson(albumDoc, albumResult.payload);
    albumUri = albumDoc["items"][0]["uri"].as<String>();
    albumName = albumDoc["items"][0]["name"].as<String>();
  }

  if (albumUri.length() > 0) {
    LOG("[Main] Now playing: " + albumName);
    playSpotifyUri(albumUri);
  } else {
    LOG("[Main] Failed to find a matching album for this tap.");
  }
}


void showAlbumArt() {
  // 1) GET currently-playing JSON
  HttpResult now = spotify.CallAPI(
    "GET",
    "https://api.spotify.com/v1/me/player/currently-playing",
    ""
  );
  if (now.httpCode != 200) {
    LOG("[Main] couldn’t get now-playing (HTTP " + String(now.httpCode) + ")");
    return;
  }

  // 2) Parse out the image URL
  DynamicJsonDocument doc(16 * 1024);
  deserializeJson(doc, now.payload);
  const char* url = doc["item"]["album"]["images"][1]["url"];
  LOG("[Main] cover URL: " + String(url));

  // 3) NEW: Ask the Spotify client to download the JPEG into our buffer
  size_t count = spotify.DownloadFile(String(url), jpgBuf, MAX_JPEG);
  
  LOG("[Main] Read " + String(count) + " bytes for album art");
  if (count == 0) {
      LOG("[Main] Cover download failed");
      return;
  }
  
  // 4) Decode and Render
  tft.fillScreen(ILI9341_BLACK);
  JpegDec.abort();
  if (!JpegDec.decodeArray(jpgBuf, count)) {
    LOG("[Main] JPEG decode failed");
    return;
  }

  renderJPEG(0, -30);
  lastArtMillis = millis();
}

void renderJPEG(int xPos, int yPos) {
  while (JpegDec.read()) {
    uint16_t *pImg = JpegDec.pImage;
    int mcu_w = JpegDec.MCUWidth;
    int mcu_h = JpegDec.MCUHeight;
    int mcu_x_offset = JpegDec.MCUx * mcu_w;
    int mcu_y_offset = JpegDec.MCUy * mcu_h;
    for (int y = 0; y < mcu_h; y++) {
      for (int x = 0; x < mcu_w; x++) {
        tft.drawPixel(xPos + mcu_x_offset + x, yPos + mcu_y_offset + y, pImg[y * mcu_w + x]);
      }
    }
  }
}
