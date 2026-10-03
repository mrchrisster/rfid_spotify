#include <Arduino.h>
#include <Adafruit_ILI9341.h>
#include <JPEGDecoder.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <SPI.h>
#include <vector>
#include <deque>
#include <algorithm>      // for std::swap
#include <ArduinoJson.h>
#include "MFRC522.h"
#include "SpotifyClient.h"
#include "settings.h"     // your ssid, pass, clientId, clientSecret, deviceName, refreshToken



#define MAX_JPEG   (64 * 1024)
static uint8_t jpgBuf[MAX_JPEG];

#define TFT_CS    15
#define TFT_DC     2
#define TFT_RST   22
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

static uint32_t lastArtMillis = 0;    // when we last showed art

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

// Global to cache the chosen Spotify device once
static String currentDeviceId;

void setup() {
  Serial.begin(115200);
  LOG("[Main] Setup started");

  SPI.begin(18, 19, 23);
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);

  connectWifi();
  telnetServer.begin();
  telnetServer.setNoDelay(true);
  LOG("[Telnet] Server started on port 23");

  mfrc522.PCD_Init();
  LOG("[Main] MFRC522 ready");

  // Prime Spotify token & deviceId *once*
  if (!spotify.EnsureTokenFresh()) {
    LOG("[Main] WARNING: initial token fetch failed");
  } else {
    currentDeviceId = spotify.GetDevices();
    LOG("[Main] Using device ID: " + currentDeviceId);
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

  // 30-minute idle → clear screen once
  if (lastArtMillis
      && millis() - lastArtMillis > 30UL * 60UL * 1000UL) {
    tft.fillScreen(ILI9341_BLACK);
    lastArtMillis = 0;
    LOG("[Main] screen cleared after 30 min");
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
    // simulate play
    // int code = 200; 

    if (code == 200 || code == 204) {
      LOG("[Main] Playback OK");
      showAlbumArt();
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
  spotify.EnsureTokenFresh();
  if (currentDeviceId.length() == 0) {
    LOG("[Main] No device for disableShuffle");
    return;
  }
  String url = "https://api.spotify.com/v1/me/player/shuffle?state=false"
               "&device_id=" + currentDeviceId;
  HttpResult r = spotify.CallAPI("PUT", url, "{}");
  if (r.httpCode == 200 || r.httpCode == 204) {
    LOG("[Main] Shuffle OFF");
  } else {
    logError("disableShuffle", r.httpCode);
  }
}


// ─── fetch & render the current track’s album‐art ───────────────────
void showAlbumArt() {
  // 1) GET currently‐playing JSON
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

  // 3) Download JPEG into jpgBuf…
  WiFiClientSecure artClient;
  artClient.setCACert(digicert_root_ca);

  HTTPClient http;
  http.begin(artClient, url);
  int code = http.GET();
  LOG("[Main] cover download HTTP code: " + String(code));
  if (code != HTTP_CODE_OK) {
    LOG("[Main] cover download failed");
    http.end();
    return;
  }
  WiFiClient& stream = http.getStream();

  size_t count = 0;
  unsigned long start = millis();
  while ((millis() - start) < 5000 && (http.connected() || stream.available())) {
    while (stream.available() && count < MAX_JPEG) {
      jpgBuf[count++] = stream.read();
    }
    delay(1);
  }
  http.end();
  if (count == 0) {
    LOG("[Main] no data read");
    return;
  }
  LOG("[Main] read " + String(count) + " bytes");

  // 4) Decode
  tft.fillScreen(ILI9341_BLACK);
  JpegDec.abort();
  if (!JpegDec.decodeArray(jpgBuf, count)) {
    LOG("[Main] JPEG decode failed");
    return;
  }

  // 5) Render with a –30px Y offset (crop the top 30 pixels,
  // so rows 30…269 of the 300px cover fill rows 0…239 of the 240px display)
  renderJPEG(0, -30);

  lastArtMillis = millis();
}



void renderJPEG(int xPos, int yPos) {
  // JpegDec.MCUWidth/MCUHeight = size of this block
  while (JpegDec.read()) {
    uint16_t *pImg   = JpegDec.pImage;
    int mcu_w        = JpegDec.MCUWidth;
    int mcu_h        = JpegDec.MCUHeight;
    int mcu_x_offset = JpegDec.MCUx * mcu_w;
    int mcu_y_offset = JpegDec.MCUy * mcu_h;

    for (int y = 0; y < mcu_h; y++) {
      for (int x = 0; x < mcu_w; x++) {
        uint16_t color = pImg[y * mcu_w + x];
        tft.drawPixel(xPos + mcu_x_offset + x,
                      yPos + mcu_y_offset + y,
                      color);
      }
    }
  }
}


// ——— True no-repeat shuffle per artist ———
void playRandomAlbumFromArtist(const String& artistUri) {
  // 1) strip "spotify:artist:" prefix
  const char* prefix = "spotify:artist:";
  String artistId = artistUri.substring(strlen(prefix));

  // 2) fetch total number of albums
  String countUrl =
    "https://api.spotify.com/v1/artists/" + artistId +
    "/albums?include_groups=album&limit=1&offset=0&market=CA";
  HttpResult cr = spotify.CallAPI("GET", countUrl, "");
  if (cr.httpCode != 200) {
    logError("fetch total albums", cr.httpCode);
    return;
  }
  DynamicJsonDocument cdoc(4096);
  if (deserializeJson(cdoc, cr.payload)) {
    LOG("[Main] JSON parse failed on total");
    return;
  }
  int total = cdoc["total"].as<int>();
  if (total <= 0) {
    LOG("[Main] No albums found for artist");
    return;
  }

  // 3) pick a random index in [0, total)
  int rnd = random(0, total);
  LOG("[Main] Random album index: " + String(rnd) + " of " + String(total));

  // 4) compute page offset & index within page (pageSize = 20)
  const int pageSize    = 20;
  int pageOffset        = (rnd / pageSize) * pageSize;
  int indexInPage       = rnd % pageSize;

  // 5) fetch that page, but only request the "uri" and "total" fields
  String pageUrl =
    "https://api.spotify.com/v1/artists/" + artistId +
    "/albums?include_groups=album" +
    "&market=CA" +
    "&limit="  + String(pageSize) +
    "&offset=" + String(pageOffset) +
    "&fields=items(uri),total";

  HttpResult pr = spotify.CallAPI("GET", pageUrl, "");
  LOG("[Main] Albums page HTTP code: " + String(pr.httpCode));
  LOG("[Main] Albums page payload: " + pr.payload);
  if (pr.httpCode != 200) {
    logError("fetch albums page", pr.httpCode);
    return;
  }

  // 6) parse the filtered JSON with a small buffer
  const size_t capacity =
      JSON_OBJECT_SIZE(2)            // root object: { total, items }
    + JSON_ARRAY_SIZE(pageSize)     // items array
    + pageSize * JSON_OBJECT_SIZE(1) // each { "uri" }
    + 1024;                          // slack
  DynamicJsonDocument pd(capacity);
  auto err = deserializeJson(pd, pr.payload);
  if (err) {
    LOG(String("[Main] JSON parse failed on page: ") + err.c_str());
    return;
  }

  // 7) select the URI at the random index
  JsonArray items = pd["items"].as<JsonArray>();
  if (indexInPage >= items.size()) {
    LOG("[Main] Invalid page data");
    return;
  }
  String uri = items[indexInPage]["uri"].as<String>();
  LOG("[Main] Selected album URI: " + uri);

  // 8) play the chosen album
  playSpotifyUri(uri);
}
