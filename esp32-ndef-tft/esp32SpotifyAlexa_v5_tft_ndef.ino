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
#include <Preferences.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "MFRC522.h"
#include "NfcAdapter.h"      // Added for NDEF support
#include "SpotifyClient.h"
#include "settings.h"

#define MAX_JPEG   (64 * 1024)
static uint8_t jpgBuf[MAX_JPEG];
static size_t lastJpgCount = 0;

Preferences preferences;
WebServer webServer(80);

// --- Screen Definitions ---
#define TFT_CS    15
#define TFT_DC     2
#define TFT_RST   22
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
static uint32_t lastScreenActivityMillis = 0;

// --- Log-history for Telnet replay & Web console ---
static const size_t MAX_LOG_HISTORY = 100;
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

bool checkAndRecoverMFRC522(bool forceReset = false) {
  // Deselect TFT CS before MFRC522 SPI transaction
  digitalWrite(TFT_CS, HIGH);

  byte version = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  // Valid MFRC522 versions: 0x90, 0x91, 0x92, 0x88, 0x12. 0x00 or 0xFF indicates failure/freeze.
  bool isHung = (version == 0x00 || version == 0xFF);

  if (isHung || forceReset) {
    if (forceReset) {
      LOG("[RFID] Hardware reset pulse applied to MFRC522...");
    } else {
      LOG("[RFID] WARNING: Reader locked up (VersionReg: 0x" + String(version, HEX) + "). Triggering auto-recovery...");
    }

    // Hardware electrical reset pulse on RST_PIN
    pinMode(SS_PIN, OUTPUT);
    digitalWrite(SS_PIN, HIGH);
    pinMode(RST_PIN, OUTPUT);
    digitalWrite(RST_PIN, LOW);
    delay(50);
    digitalWrite(RST_PIN, HIGH);
    delay(50);

    mfrc522.PCD_Init();
    delay(10);
    nfc.begin();

    byte newVer = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
    if (newVer != 0x00 && newVer != 0xFF) {
      LOG("[RFID] Recovery SUCCESS: MFRC522 active (VersionReg: 0x" + String(newVer, HEX) + ")");
      return true;
    } else {
      LOG("[RFID] Recovery WARNING: MFRC522 still unresponsive (VersionReg: 0x" + String(newVer, HEX) + ")");
      return false;
    }
  }
  return true;
}

// --- Spotify client ---
SpotifyClient spotify(clientId, clientSecret, deviceName, refreshToken);
static String currentDeviceId = "";

// --- Forward declarations ---
void handleApiResetRfid();
void handleWebRoot();
void handleWebUpdate();
void handleApiLogs();
void handleApiRestart();
void handleApiRescan();
void handleApiNext();
void handleApiPlayPause();
void handleApiVolumeUp();
void handleApiVolumeDown();
void handleApiDevices();
void handleApiSelectDevice();
void handleApiStatus();
void handleApiClearScreen();
void handleApiRefreshToken();
void printTelnetHelp();
void printTelnetStatus();
void togglePlayPause();
void adjustVolume(int delta);
void updateCpuLoad(unsigned long activeMicros);
void handleTelnet();
void connectWifi();
void ensureWifiConnected();
void logError(const String& msg, int code);
void readNFCTag();
void playSpotifyUri(const String& uri);
void disableShuffle();
void playRandomAlbumFromArtist(const String& artistUri);
void showAlbumArt();
void showLastImage();
void showDeviceInfoScreen();
void handleApiShowInfo();
void renderJPEG(int xPos, int yPos);

void onSpotifyTokenRotated(const String& newToken) {
  preferences.putString("ref_token", newToken);
  LOG("[Main] Rolling refresh token automatically saved to NVS flash: " + newToken.substring(0, 10) + "...");
}

void setup() {
  Serial.begin(115200);
  LOG("[Main] Setup started");

  SPI.begin(18, 19, 23); // Correct SPI pins for your device
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);

  connectWifi();
  configTime(0, 0, "pool.ntp.org", "time.nist.gov"); // Sync clock for TLS/NTP

  if (MDNS.begin("spotify-player")) {
    MDNS.addService("http", "tcp", 80);
    LOG("[Main] mDNS active: http://spotify-player.local/");
  }

  // Display boot diagnostics on TFT
  showDeviceInfoScreen();

  telnetServer.begin();
  telnetServer.setNoDelay(true);
  LOG("[Telnet] Server started on port 23");

  // Hook up automatic rolling token persistence
  spotify.SetRefreshTokenCallback(onSpotifyTokenRotated);

  // Load or seed refresh token from persistent NVS flash
  preferences.begin("spotify", false);
  String savedToken = preferences.getString("ref_token", "");
  if (refreshToken.length() > 0 && refreshToken != savedToken) {
    LOG("[Main] Updating NVS flash with new token from settings.h");
    preferences.putString("ref_token", refreshToken);
    spotify.SetRefreshToken(refreshToken);
  } else if (savedToken.length() > 0) {
    LOG("[Main] Loaded refresh token from NVS flash");
    spotify.SetRefreshToken(savedToken);
  } else if (refreshToken.length() > 0) {
    LOG("[Main] Initializing NVS flash with settings.h refresh token");
    preferences.putString("ref_token", refreshToken);
    spotify.SetRefreshToken(refreshToken);
  }

  // Load custom speaker preference from NVS if saved
  String savedSpeaker = preferences.getString("device_name", "");
  if (!savedSpeaker.isEmpty()) {
    deviceName = savedSpeaker;
    LOG("[Main] Loaded target speaker from NVS: " + deviceName);
  }

  // Setup Web Portal & REST endpoints
  webServer.on("/", HTTP_GET, handleWebRoot);
  webServer.on("/update", HTTP_POST, handleWebUpdate);
  webServer.on("/api/token", HTTP_POST, handleWebUpdate);
  webServer.on("/api/logs", HTTP_GET, handleApiLogs);
  webServer.on("/api/restart", HTTP_POST, handleApiRestart);
  webServer.on("/api/rescan", HTTP_POST, handleApiRescan);
  webServer.on("/api/next", HTTP_POST, handleApiNext);
  webServer.on("/api/playpause", HTTP_POST, handleApiPlayPause);
  webServer.on("/api/volup", HTTP_POST, handleApiVolumeUp);
  webServer.on("/api/voldown", HTTP_POST, handleApiVolumeDown);
  webServer.on("/api/devices", HTTP_GET, handleApiDevices);
  webServer.on("/api/select_device", HTTP_POST, handleApiSelectDevice);
  webServer.on("/api/status", HTTP_GET, handleApiStatus);
  webServer.on("/api/clear_screen", HTTP_POST, handleApiClearScreen);
  webServer.on("/api/show_image", HTTP_POST, handleApiShowImage);
  webServer.on("/api/show_info", HTTP_POST, handleApiShowInfo);
  webServer.on("/api/reset_rfid", HTTP_POST, handleApiResetRfid);
  webServer.on("/api/refresh_token", HTTP_POST, handleApiRefreshToken);
  webServer.begin();
  LOG("[Web] HTTP portal ready at http://" + WiFi.localIP().toString() + "/ or http://spotify-player.local/");

  // Physical hardware reset of MFRC522 on boot
  pinMode(SS_PIN, OUTPUT);
  digitalWrite(SS_PIN, HIGH);
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  checkAndRecoverMFRC522(true);

  if (!spotify.EnsureTokenFresh()) {
    LOG("[Main] WARNING: initial token fetch failed");
  } else {
    currentDeviceId = spotify.GetDevices();
    LOG("[Main] Stored Device ID: " + currentDeviceId);
  }
}

void loop() {
  unsigned long workStart = micros();

  handleTelnet();
  webServer.handleClient();
  ensureWifiConnected();

  static bool wasDisconnected = false;
  if (WiFi.status() != WL_CONNECTED) {
    wasDisconnected = true;
  } else if (wasDisconnected) {
    LOG("[Main] Wi-Fi reconnected (" + WiFi.localIP().toString() + ") -> re-binding services");
    spotify.ResetState();
    currentDeviceId = ""; // Force re-fetch of device ID after reconnect
    webServer.close();
    webServer.begin();
    telnetServer.begin();
    MDNS.end();
    MDNS.begin("spotify-player");
    wasDisconnected = false;
  }

  // Periodic MFRC522 card reader health check and auto-recovery (every 30 seconds)
  static unsigned long lastRfidCheck = 0;
  if (millis() - lastRfidCheck > 30000UL) {
    lastRfidCheck = millis();
    checkAndRecoverMFRC522(false);
  }

  // Daily keep-alive: Keeps Spotify token fresh and resets Spotify's 180-day inactivity timer
  static unsigned long lastKeepAlive = 0;
  if (millis() - lastKeepAlive > 24UL * 60UL * 60UL * 1000UL) {
    lastKeepAlive = millis();
    LOG("[Main] Performing daily Spotify token keep-alive check...");
    spotify.EnsureTokenFresh();
  }

  readNFCTag(); // Replaced the old check with the new function call

  // 10-minute idle -> clear screen to prevent TFT image persistence & save power
  if (lastScreenActivityMillis && (millis() - lastScreenActivityMillis > 10UL * 60UL * 1000UL)) {
    digitalWrite(SS_PIN, HIGH);
    tft.fillScreen(ILI9341_BLACK);
    lastScreenActivityMillis = 0;
    LOG("[Main] Screen blanked after 10 min idle to protect TFT");
  }

  unsigned long workElapsed = micros() - workStart;
  updateCpuLoad(workElapsed);

  delay(20); // Small delay to yield to Wi-Fi and web stack
}

// --- CPU Load Tracking & Helpers ---
static unsigned long activeWorkMicrosAcc = 0;
static unsigned long lastCpuReportMillis = 0;
static float cpuLoadPercent = 0.0f;

void updateCpuLoad(unsigned long activeMicros) {
  activeWorkMicrosAcc += activeMicros;
  unsigned long now = millis();
  if (now - lastCpuReportMillis >= 1000) {
    unsigned long totalElapsedMicros = (now - lastCpuReportMillis) * 1000UL;
    if (totalElapsedMicros > 0) {
      cpuLoadPercent = ((float)activeWorkMicrosAcc / (float)totalElapsedMicros) * 100.0f;
      if (cpuLoadPercent > 100.0f) cpuLoadPercent = 100.0f;
    }
    activeWorkMicrosAcc = 0;
    lastCpuReportMillis = now;
  }
}

// --- Interactive Commands & Helpers ---
static int currentVolume = 50;
static bool isPaused = false;

void togglePlayPause() {
  if (currentDeviceId.isEmpty()) currentDeviceId = spotify.GetDevices();
  if (currentDeviceId.isEmpty()) {
    LOG("[Main] Cannot toggle playback: Device ID empty.");
    return;
  }
  if (!isPaused) {
    LOG("[Main] Pausing playback...");
    spotify.CallAPI("PUT", "https://api.spotify.com/v1/me/player/pause?device_id=" + currentDeviceId, "");
    isPaused = true;
  } else {
    LOG("[Main] Resuming playback...");
    spotify.CallAPI("PUT", "https://api.spotify.com/v1/me/player/play?device_id=" + currentDeviceId, "");
    isPaused = false;
  }
}

void adjustVolume(int delta) {
  if (currentDeviceId.isEmpty()) currentDeviceId = spotify.GetDevices();
  if (currentDeviceId.isEmpty()) {
    LOG("[Main] Cannot adjust volume: Device ID empty.");
    return;
  }
  currentVolume = constrain(currentVolume + delta, 0, 100);
  LOG("[Main] Volume set to " + String(currentVolume) + "%");
  spotify.CallAPI("PUT", "https://api.spotify.com/v1/me/player/volume?volume_percent=" + String(currentVolume) + "&device_id=" + currentDeviceId, "");
}

void printTelnetHelp() {
  if (!telnetClient || !telnetClient.connected()) return;
  telnetClient.println("\n+-------------------------------------------------------+");
  telnetClient.println("|         Spotify RFID Player - Keyboard Commands       |");
  telnetClient.println("+-------------------------------------------------------+");
  telnetClient.println("| [?] or [h] : Show this help menu                      |");
  telnetClient.println("| [s]        : Show system status (CPU, RAM, Uptime)    |");
  telnetClient.println("| [d]        : Discover & list all Spotify speakers     |");
  telnetClient.println("| [t]        : Test & force Spotify token refresh       |");
  telnetClient.println("| [n]        : Skip to Next track                       |");
  telnetClient.println("| [p]        : Play / Pause toggle                      |");
  telnetClient.println("| [+] / [-]  : Volume Up (+10%) / Down (-10%)           |");
  telnetClient.println("| [c]        : Clear TFT screen                         |");
  telnetClient.println("| [i] or [a] : Show last/current album art on TFT       |");
  telnetClient.println("| [w]        : Show device info / status on TFT         |");
  telnetClient.println("| [k]        : Test & reset MFRC522 card reader         |");
  telnetClient.println("| [l]        : Replay full log history                  |");
  telnetClient.println("| [r]        : Reboot ESP32                             |");
  telnetClient.println("+-------------------------------------------------------+\n");
}

void printTelnetStatus() {
  if (!telnetClient || !telnetClient.connected()) return;
  unsigned long sec = millis() / 1000;
  unsigned long days = sec / 86400; sec %= 86400;
  unsigned long hrs = sec / 3600; sec %= 3600;
  unsigned long mins = sec / 60; sec %= 60;

  byte rfidVer = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);

  telnetClient.println("\n================ SYSTEM STATUS ================");
  telnetClient.printf(" CPU Load      : %.1f%% (@ %d MHz)\n", cpuLoadPercent, ESP.getCpuFreqMHz());
  telnetClient.printf(" Free Heap RAM : %u bytes (Min: %u)\n", ESP.getFreeHeap(), ESP.getMinFreeHeap());
  telnetClient.printf(" Uptime        : %lud %luh %lum %lus\n", days, hrs, mins, sec);
  telnetClient.println(" Wi-Fi SSID    : " + String(ssid) + " (" + String(WiFi.RSSI()) + " dBm)");
  telnetClient.println(" ESP32 IP      : " + WiFi.localIP().toString());
  telnetClient.println(" Target Speaker: " + deviceName + " (" + (currentDeviceId.isEmpty() ? "Disconnected" : "ID: " + currentDeviceId) + ")");
  telnetClient.println(" Token Status  : " + String(spotify.IsTokenValid() ? "ACTIVE & VALID" : "EXPIRED / REVOKED"));
  telnetClient.printf(" Card Reader   : %s (VersionReg: 0x%02X)\n", (rfidVer != 0x00 && rfidVer != 0xFF) ? "ONLINE" : "FROZEN/OFFLINE", rfidVer);
  telnetClient.println(" Volume        : " + String(currentVolume) + "%");
  telnetClient.println(" Web Portal    : http://" + WiFi.localIP().toString() + "/ or http://spotify-player.local/");
  telnetClient.println("===============================================\n");
}

// --- Telnet & Web helpers ---
void handleTelnet() {
  // Clean up if client disconnected on their side
  if (telnetClient && !telnetClient.connected()) {
    telnetClient.stop();
  }

  // Handle incoming connections
  if (telnetServer.hasClient()) {
    WiFiClient newClient = telnetServer.available();
    // If a client was already connected, supersede it cleanly (no more "Busy - one client only")
    if (telnetClient && telnetClient.connected()) {
      telnetClient.println("\n[Telnet] Session taken over by new connection. Disconnecting.");
      telnetClient.stop();
    }
    telnetClient = newClient;
    telnetClient.setNoDelay(true);

    telnetClient.println("==================================================");
    telnetClient.println("  Spotify RFID Player - Live Console");
    telnetClient.println("  Type '?' or 'h' for list of keyboard commands");
    telnetClient.println("==================================================\n");

    for (auto &line : logHistory) {
      telnetClient.println(line);
    }
    LOG("[Telnet] Client connected");

    if (!spotify.IsTokenValid()) {
      telnetClient.println("\n-------------------------------------------------------------");
      telnetClient.println("[Telnet] NOTE: Spotify token is currently EXPIRED / REVOKED.");
      telnetClient.println("[Telnet] To renew without re-flashing your ESP32:");
      telnetClient.println("[Telnet]   1. Run 'python3 renew_token.py' on your Mac, OR");
      telnetClient.println("[Telnet]   2. Open http://" + WiFi.localIP().toString() + "/ in your browser");
      telnetClient.println("-------------------------------------------------------------\n");
    }
  }

  // Handle interactive keyboard commands from Telnet client
  while (telnetClient && telnetClient.connected() && telnetClient.available()) {
    char c = telnetClient.read();
    if (c == '\r' || c == '\n') continue;

    if (c == '?' || c == 'h' || c == 'H') {
      printTelnetHelp();
    } else if (c == 's' || c == 'S') {
      printTelnetStatus();
    } else if (c == 'r' || c == 'R') {
      telnetClient.println("[Telnet] Rebooting ESP32 in 1 second...");
      delay(1000);
      ESP.restart();
    } else if (c == 't' || c == 'T') {
      telnetClient.println("[Telnet] Refreshing Spotify token...");
      spotify.EnsureTokenFresh();
    } else if (c == 'd' || c == 'D') {
      telnetClient.println("[Telnet] Searching for Spotify speaker devices...");
      currentDeviceId = spotify.GetDevices();
      telnetClient.println("[Telnet] Active Device ID: " + currentDeviceId);
    } else if (c == 'n' || c == 'N') {
      telnetClient.println("[Telnet] Skipping to next track...");
      spotify.Next();
    } else if (c == 'p' || c == 'P') {
      togglePlayPause();
      telnetClient.println(isPaused ? "[Telnet] Playback paused." : "[Telnet] Playback resumed.");
    } else if (c == '+' || c == '=') {
      adjustVolume(10);
      telnetClient.println("[Telnet] Volume: " + String(currentVolume) + "%");
    } else if (c == '-' || c == '_') {
      adjustVolume(-10);
      telnetClient.println("[Telnet] Volume: " + String(currentVolume) + "%");
    } else if (c == 'c' || c == 'C') {
      telnetClient.println("[Telnet] Clearing TFT screen...");
      digitalWrite(SS_PIN, HIGH);
      tft.fillScreen(ILI9341_BLACK);
      lastScreenActivityMillis = 0;
    } else if (c == 'i' || c == 'I' || c == 'a' || c == 'A') {
      telnetClient.println("[Telnet] Showing last / current album art on TFT...");
      showLastImage();
    } else if (c == 'w' || c == 'W') {
      telnetClient.println("[Telnet] Showing device info screen on TFT...");
      showDeviceInfoScreen();
    } else if (c == 'k' || c == 'K') {
      telnetClient.println("[Telnet] Testing & resetting MFRC522 card reader...");
      bool ok = checkAndRecoverMFRC522(true);
      telnetClient.println(ok ? "[Telnet] Card reader is ONLINE & HEALTHY" : "[Telnet] Card reader FAILED recovery");
    } else if (c == 'l' || c == 'L') {
      telnetClient.println("\n--- LOG HISTORY REPLAY ---");
      for (auto &line : logHistory) {
        telnetClient.println(line);
      }
      telnetClient.println("--- END LOG REPLAY ---\n");
    } else {
      telnetClient.println("[Telnet] Unknown key '" + String(c) + "'. Press '?' for commands.");
    }
  }
}

void handleApiLogs() {
  String out = "";
  for (auto &line : logHistory) {
    out += line + "\n";
  }
  webServer.send(200, "text/plain", out);
}

void handleApiRestart() {
  webServer.send(200, "application/json", "{\"status\":\"ok\",\"message\":\"Rebooting\"}");
  delay(500);
  ESP.restart();
}

void handleApiRescan() {
  currentDeviceId = spotify.GetDevices();
  webServer.send(200, "application/json", "{\"status\":\"ok\",\"deviceId\":\"" + currentDeviceId + "\"}");
}

void handleApiNext() {
  spotify.Next();
  webServer.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleApiPlayPause() {
  togglePlayPause();
  webServer.send(200, "application/json", "{\"status\":\"ok\",\"isPaused\":" + String(isPaused ? "true" : "false") + "}");
}

void handleApiVolumeUp() {
  adjustVolume(10);
  webServer.send(200, "application/json", "{\"status\":\"ok\",\"volume\":" + String(currentVolume) + "}");
}

void handleApiVolumeDown() {
  adjustVolume(-10);
  webServer.send(200, "application/json", "{\"status\":\"ok\",\"volume\":" + String(currentVolume) + "}");
}

void handleApiDevices() {
  HttpResult res = spotify.CallAPI("GET", "https://api.spotify.com/v1/me/player/devices", "");
  if (res.httpCode == 200) {
    webServer.send(200, "application/json", res.payload);
  } else {
    webServer.send(res.httpCode > 0 ? res.httpCode : 500, "application/json", "{\"devices\":[]}");
  }
}

void handleApiSelectDevice() {
  if (!webServer.hasArg("plain")) {
    webServer.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing body\"}");
    return;
  }
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, webServer.arg("plain"))) {
    webServer.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Bad JSON\"}");
    return;
  }
  String newId = doc["id"].as<String>();
  String newName = doc["name"].as<String>();

  if (!newId.isEmpty()) {
    currentDeviceId = newId;
    if (!newName.isEmpty()) {
      deviceName = newName;
      preferences.putString("device_name", newName);
    }
    LOG("[Web] Target speaker switched to: " + deviceName + " (" + currentDeviceId + ")");

    // Transfer active playback to the newly selected speaker
    String transferBody = "{\"device_ids\":[\"" + newId + "\"],\"play\":true}";
    spotify.CallAPI("PUT", "https://api.spotify.com/v1/me/player", transferBody);

    webServer.send(200, "application/json", "{\"status\":\"ok\",\"deviceId\":\"" + currentDeviceId + "\",\"deviceName\":\"" + deviceName + "\"}");
  } else {
    webServer.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Device ID empty\"}");
  }
}

void handleApiClearScreen() {
  digitalWrite(SS_PIN, HIGH);
  tft.fillScreen(ILI9341_BLACK);
  lastScreenActivityMillis = 0;
  LOG("[Web] TFT screen cleared via web dashboard");
  webServer.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleApiShowImage() {
  LOG("[Web] Show last image requested via web dashboard");
  showLastImage();
  webServer.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleApiShowInfo() {
  LOG("[Web] Show device info requested via web dashboard");
  showDeviceInfoScreen();
  webServer.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleApiResetRfid() {
  LOG("[Web] Card reader reset requested via web dashboard");
  bool ok = checkAndRecoverMFRC522(true);
  webServer.send(200, "application/json", "{\"status\":\"" + String(ok ? "ok" : "error") + "\"}");
}

void handleApiRefreshToken() {
  bool ok = spotify.EnsureTokenFresh();
  webServer.send(200, "application/json", "{\"status\":\"" + String(ok ? "ok" : "error") + "\"}");
}

void handleApiStatus() {
  unsigned long sec = millis() / 1000;
  unsigned long days = sec / 86400; sec %= 86400;
  unsigned long hrs = sec / 3600; sec %= 3600;
  unsigned long mins = sec / 60; sec %= 60;
  char uptimeStr[32];
  sprintf(uptimeStr, "%lud %luh %lum %lus", days, hrs, mins, sec);

  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t totalHeap = ESP.getHeapSize();
  float heapUsedPct = 100.0f - ((float)freeHeap / (float)totalHeap * 100.0f);

  byte rfidVer = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);

  DynamicJsonDocument doc(512);
  doc["uptime"] = uptimeStr;
  doc["cpu_load"] = serialized(String(cpuLoadPercent, 1));
  doc["cpu_freq_mhz"] = ESP.getCpuFreqMHz();
  doc["free_heap"] = freeHeap;
  doc["total_heap"] = totalHeap;
  doc["heap_used_pct"] = serialized(String(heapUsedPct, 1));
  doc["wifi_ssid"] = ssid;
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["ip"] = WiFi.localIP().toString();
  doc["device_name"] = deviceName;
  doc["device_id"] = currentDeviceId;
  doc["token_valid"] = spotify.IsTokenValid();
  doc["volume"] = currentVolume;
  doc["is_paused"] = isPaused;
  doc["rfid_ok"] = (rfidVer != 0x00 && rfidVer != 0xFF);
  char rfidHex[8];
  sprintf(rfidHex, "0x%02X", rfidVer);
  doc["rfid_version"] = rfidHex;

  String out;
  serializeJson(doc, out);
  webServer.send(200, "application/json", out);
}

void handleWebRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
                "<title>Spotify RFID Player</title>"
                "<style>"
                "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: #121212; color: #fff; padding: 16px; max-width: 650px; margin: 0 auto; }"
                "h1 { color: #1DB954; font-size: 24px; margin: 0 0 16px 0; display: flex; justify-content: space-between; align-items: center; }"
                ".grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(130px, 1fr)); gap: 10px; margin-bottom: 16px; }"
                ".stat-card { background: #282828; padding: 12px; border-radius: 10px; text-align: center; box-shadow: 0 2px 8px rgba(0,0,0,0.4); }"
                ".stat-val { font-size: 16px; font-weight: bold; color: #1DB954; margin-top: 4px; }"
                ".stat-lbl { font-size: 11px; color: #aaa; text-transform: uppercase; letter-spacing: 0.5px; }"
                ".card { background: #282828; padding: 18px; border-radius: 12px; margin-bottom: 16px; box-shadow: 0 4px 12px rgba(0,0,0,0.5); }"
                "select, input[type=text] { width: 100%; padding: 12px; margin: 8px 0; box-sizing: border-box; background: #3e3e3e; border: 1px solid #555; color: #fff; border-radius: 8px; font-size: 14px; }"
                "input[type=submit], .btn { display: inline-flex; align-items: center; justify-content: center; gap: 6px; background: #1DB954; color: white; border: none; padding: 9px 15px; border-radius: 20px; font-weight: bold; cursor: pointer; font-size: 13px; margin: 4px 2px; text-decoration: none; transition: background 0.2s; }"
                ".btn:hover { filter: brightness(1.1); }"
                ".btn-secondary { background: #444; }"
                ".btn-control { background: #1e6091; }"
                ".btn-danger { background: #b7094c; }"
                ".status { padding: 10px; border-radius: 8px; margin-top: 10px; font-weight: bold; text-align: center; font-size: 13px; }"
                ".ok { background: #1b4332; color: #74c69d; }"
                ".err { background: #49111c; color: #ff758f; }"
                "pre { background: #181818; color: #74c69d; padding: 12px; border-radius: 8px; height: 260px; overflow-y: auto; font-family: monospace; font-size: 11px; white-space: pre-wrap; line-height: 1.4; border: 1px solid #333; margin: 10px 0 0 0; }"
                "</style></head><body>"
                "<h1><span>Spotify RFID Player</span><span id='tokenPill' style='font-size:12px; padding:4px 10px; border-radius:12px; background:#1b4332; color:#74c69d;'>CONNECTED</span></h1>"
                
                "<div class='grid'>"
                "<div class='stat-card'><div class='stat-lbl'>CPU Load</div><div class='stat-val' id='valCpu'>--%</div></div>"
                "<div class='stat-card'><div class='stat-lbl'>Free RAM</div><div class='stat-val' id='valRam'>-- KB</div></div>"
                "<div class='stat-card'><div class='stat-lbl'>Card Reader</div><div class='stat-val' id='valRfid'>--</div></div>"
                "<div class='stat-card'><div class='stat-lbl'>Wi-Fi Signal</div><div class='stat-val' id='valWifi'>-- dBm</div></div>"
                "<div class='stat-card'><div class='stat-lbl'>Uptime</div><div class='stat-val' id='valUptime'>--</div></div>"
                "</div>"

                "<div class='card'>"
                "<h3 style='margin-top:0;'>Target Speaker</h3>"
                "<p style='margin:4px 0 10px 0;'><strong>Current:</strong> <span id='curSpeaker' style='color:#1DB954; font-weight:bold;'>" + deviceName + "</span></p>"
                "<label style='font-size:12px; color:#aaa;'>Select from detected Spotify Connect speakers:</label>"
                "<select id='speakerSelect'><option value=''>Loading speakers...</option></select>"
                "<div style='margin-top:8px;'>"
                "<button class='btn btn-control' onclick='switchSpeaker()'>Switch Speaker</button>"
                "<button class='btn btn-secondary' onclick='loadDevices()'><svg width='13' height='13' viewBox='0 0 24 24' fill='currentColor'><path d='M17.65 6.35A7.958 7.958 0 0012 4c-4.42 0-7.99 3.58-7.99 8s3.57 8 7.99 8c3.73 0 6.84-2.55 7.73-6h-2.08A5.99 5.99 0 0112 18c-3.31 0-6-2.69-6-6s2.69-6 6-6c1.66 0 3.14.69 4.22 1.78L13 11h7V4l-2.35 2.35z'/></svg> Refresh List</button>"
                "</div></div>"

                "<div class='card'>"
                "<h3 style='margin-top:0;'>Player & Volume Controls</h3>"
                "<div>"
                "<button class='btn btn-control' onclick='apiPost(\"/api/playpause\")'><svg width='13' height='13' viewBox='0 0 24 24' fill='currentColor'><path d='M4 5v14l9-7-9-7zm11 0h3v14h-3V5z'/></svg> Play/Pause</button>"
                "<button class='btn btn-control' onclick='apiPost(\"/api/next\")'><svg width='13' height='13' viewBox='0 0 24 24' fill='currentColor'><path d='M6 18l8.5-6L6 6v12zM16 6v12h2V6h-2z'/></svg> Next Track</button>"
                "<button class='btn btn-secondary' onclick='apiPost(\"/api/voldown\")'><svg width='13' height='13' viewBox='0 0 24 24' fill='currentColor'><path d='M7 9v6h4l5 5V4l-5 5H7z'/></svg> Vol &minus;</button>"
                "<button class='btn btn-secondary' onclick='apiPost(\"/api/volup\")'><svg width='13' height='13' viewBox='0 0 24 24' fill='currentColor'><path d='M3 9v6h4l5 5V4L7 9H3zm13.5 3c0-1.77-1.02-3.29-2.5-4.03v8.05c1.48-.73 2.5-2.25 2.5-4.02zM14 3.23v2.06c2.89.86 5 3.54 5 6.71s-2.11 5.85-5 6.71v2.06c4.01-.91 7-4.49 7-8.77s-2.99-7.86-7-8.77z'/></svg> Vol &#43;</button>"
                "</div>"
                "<div style='margin-top:10px; padding-top:10px; border-top:1px solid #3a3a3a;'>"
                "<button class='btn btn-secondary' onclick='apiPost(\"/api/clear_screen\")'><svg width='13' height='13' viewBox='0 0 24 24' fill='currentColor'><path d='M19 4h-3.5l-1-1h-5l-1 1H5v2h14M6 19a2 2 0 002 2h8a2 2 0 002-2V7H6v12z'/></svg> Clear Display</button>"
                "<button class='btn btn-secondary' onclick='apiPost(\"/api/show_image\")'><svg width='13' height='13' viewBox='0 0 24 24' fill='currentColor'><path d='M21 19V5c0-1.1-.9-2-2-2H5c-1.1 0-2 .9-2 2v14c0 1.1.9 2 2 2h14c1.1 0 2-.9 2-2zM8.5 13.5l2.5 3.01L14.5 12l4.5 6H5l3.5-4.5z'/></svg> Show Last Image</button>"
                "<button class='btn btn-secondary' onclick='apiPost(\"/api/show_info\")'><svg width='13' height='13' viewBox='0 0 24 24' fill='currentColor'><path d='M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm1 15h-2v-6h2v6zm0-8h-2V7h2v2z'/></svg> Device Info</button>"
                "<button class='btn btn-secondary' onclick='apiPost(\"/api/reset_rfid\")'><svg width='13' height='13' viewBox='0 0 24 24' fill='currentColor'><path d='M17.65 6.35A7.958 7.958 0 0012 4c-4.42 0-7.99 3.58-7.99 8s3.57 8 7.99 8c3.73 0 6.84-2.55 7.73-6h-2.08A5.99 5.99 0 0112 18c-3.31 0-6-2.69-6-6s2.69-6 6-6c1.66 0 3.14.69 4.22 1.78L13 11h7V4l-2.35 2.35z'/></svg> Reset Card Reader</button>"
                "<button class='btn btn-secondary' onclick='apiPost(\"/api/refresh_token\")'><svg width='13' height='13' viewBox='0 0 24 24' fill='currentColor'><path d='M12.65 10C11.83 7.67 9.61 6 7 6c-3.31 0-6 2.69-6 6s2.69 6 6 6c2.61 0 4.83-1.67 5.65-4H17v4h4v-4h2v-4H12.65zM7 14c-1.1 0-2-.9-2-2s.9-2 2-2 2 .9 2 2-.9 2-2 2z'/></svg> Refresh Token</button>"
                "<button class='btn btn-danger' onclick='restartDevice()'><svg width='13' height='13' viewBox='0 0 24 24' fill='currentColor'><path d='M17.65 6.35A7.958 7.958 0 0012 4c-4.42 0-7.99 3.58-7.99 8s3.57 8 7.99 8c3.73 0 6.84-2.55 7.73-6h-2.08A5.99 5.99 0 0112 18c-3.31 0-6-2.69-6-6s2.69-6 6-6c1.66 0 3.14.69 4.22 1.78L13 11h7V4l-2.35 2.35z'/></svg> Reboot ESP32</button>"
                "</div></div>"

                "<div class='card'>"
                "<div style='display:flex; justify-content:space-between; align-items:center;'>"
                "<h3 style='margin:0;'>Live Console Logs</h3>"
                "<span style='font-size:11px; color:#aaa;'>Auto-refreshing</span>"
                "</div>"
                "<pre id='logBox'>Loading logs...</pre>"
                "</div>"

                "<div class='card'>"
                "<h3 style='margin-top:0;'>Manual Token Update</h3>"
                "<p style='color:#aaa;font-size:12px; margin:4px 0 8px 0;'>Paste new Spotify Refresh Token if ever revoked:</p>"
                "<form action='/update' method='POST'>"
                "<input type='text' name='token' placeholder='Paste refresh token' required>"
                "<input type='submit' value='Save to Flash'>"
                "</form></div>"

                "<script>"
                "function fetchStatus() {"
                "  fetch('/api/status').then(r => r.json()).then(d => {"
                "    document.getElementById('valCpu').innerText = d.cpu_load + '% (' + d.cpu_freq_mhz + 'MHz)';"
                "    document.getElementById('valRam').innerText = Math.round(d.free_heap/1024) + ' KB (' + d.heap_used_pct + '% used)';"
                "    var rfidEl = document.getElementById('valRfid');"
                "    if (rfidEl) {"
                "      rfidEl.innerText = d.rfid_ok ? ('ONLINE (' + d.rfid_version + ')') : 'OFFLINE';"
                "      rfidEl.style.color = d.rfid_ok ? '#74c69d' : '#ff758f';"
                "    }"
                "    document.getElementById('valWifi').innerText = d.wifi_ssid + ' (' + d.wifi_rssi + ' dBm)';"
                "    document.getElementById('valUptime').innerText = d.uptime;"
                "    document.getElementById('curSpeaker').innerText = d.device_name + (d.device_id ? '' : ' (sleeping/unlinked)');"
                "    var pill = document.getElementById('tokenPill');"
                "    if (d.token_valid) {"
                "      pill.style.background = '#1b4332'; pill.style.color = '#74c69d'; pill.innerText = 'AUTHENTICATED';"
                "    } else {"
                "      pill.style.background = '#49111c'; pill.style.color = '#ff758f'; pill.innerText = 'TOKEN EXPIRED';"
                "    }"
                "  }).catch(() => {});"
                "}"
                "function fetchLogs() {"
                "  fetch('/api/logs').then(r => r.text()).then(t => {"
                "    var b = document.getElementById('logBox');"
                "    var atBottom = (b.scrollHeight - b.scrollTop - b.clientHeight < 60);"
                "    b.innerText = t;"
                "    if (atBottom) b.scrollTop = b.scrollHeight;"
                "  }).catch(() => {});"
                "}"
                "function loadDevices() {"
                "  fetch('/api/devices').then(r => r.json()).then(d => {"
                "    var sel = document.getElementById('speakerSelect');"
                "    sel.innerHTML = '';"
                "    var list = d.devices || [];"
                "    if (list.length === 0) {"
                "      sel.innerHTML = '<option value=\"\">(No active speakers found - wake with \"Alexa, Spotify Connect\")</option>';"
                "      return;"
                "    }"
                "    list.forEach(dev => {"
                "      var opt = document.createElement('option');"
                "      opt.value = JSON.stringify({id: dev.id, name: dev.name});"
                "      opt.innerText = dev.name + ' (' + dev.type + ')' + (dev.is_active ? ' [Active]' : '');"
                "      sel.appendChild(opt);"
                "    });"
                "  }).catch(() => {});"
                "}"
                "function switchSpeaker() {"
                "  var sel = document.getElementById('speakerSelect');"
                "  if (!sel.value) return;"
                "  var val = JSON.parse(sel.value);"
                "  fetch('/api/select_device', {"
                "    method: 'POST',"
                "    headers: {'Content-Type': 'application/json'},"
                "    body: JSON.stringify(val)"
                "  }).then(r => r.json()).then(res => {"
                "    alert('Target speaker switched to: ' + res.deviceName);"
                "    fetchStatus();"
                "    loadDevices();"
                "  });"
                "}"
                "function apiPost(url) {"
                "  fetch(url, {method:'POST'}).then(() => { fetchStatus(); fetchLogs(); });"
                "}"
                "function restartDevice() {"
                "  if (confirm('Reboot ESP32?')) {"
                "    fetch('/api/restart', {method:'POST'});"
                "    alert('Rebooting... Page will reload in 5 seconds.');"
                "    setTimeout(() => location.reload(), 5000);"
                "  }"
                "}"
                "var pollTimer = null;"
                "function pollCycle() { fetchStatus(); fetchLogs(); }"
                "function startPolling() { if (!pollTimer) pollTimer = setInterval(pollCycle, 3000); }"
                "function stopPolling() { if (pollTimer) { clearInterval(pollTimer); pollTimer = null; } }"
                "document.addEventListener('visibilitychange', () => {"
                "  if (document.hidden) stopPolling();"
                "  else { pollCycle(); startPolling(); }"
                "});"
                "pollCycle();"
                "loadDevices();"
                "startPolling();"
                "</script></body></html>";
  webServer.send(200, "text/html; charset=utf-8", html);
}

void handleWebUpdate() {
  String newToken = "";
  if (webServer.hasArg("token")) {
    newToken = webServer.arg("token");
  } else if (webServer.hasArg("plain")) {
    DynamicJsonDocument doc(1024);
    if (!deserializeJson(doc, webServer.arg("plain"))) {
      newToken = doc["token"].as<String>();
    }
  }

  newToken.trim();
  if (newToken.isEmpty()) {
    webServer.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Token is empty\"}");
    return;
  }

  LOG("[Web] Received new refresh token via HTTP: " + newToken.substring(0, 10) + "...");
  preferences.putString("ref_token", newToken);
  spotify.SetRefreshToken(newToken);

  if (spotify.EnsureTokenFresh()) {
    currentDeviceId = spotify.GetDevices();
    LOG("[Web] Token refreshed successfully! New Device ID: " + currentDeviceId);
    String resHtml = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
                     "<style>body{background:#121212;color:#fff;font-family:sans-serif;padding:30px;text-align:center;}"
                     ".btn{background:#1DB954;color:#fff;padding:14px 28px;border-radius:30px;text-decoration:none;font-weight:bold;display:inline-block;margin-top:20px;}"
                     "</style></head><body>"
                     "<h1 style='color:#1DB954;'>Token Updated!</h1>"
                     "<p>Your Spotify RFID player is re-authenticated and ready to play.</p>"
                     "<a class='btn' href='/'>Back to Status</a></body></html>";
    if (webServer.hasArg("plain")) {
      webServer.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Token updated and authenticated!\"}");
    } else {
      webServer.send(200, "text/html; charset=utf-8", resHtml);
    }
  } else {
    LOG("[Web] Token update failed: Spotify rejected the new token.");
    if (webServer.hasArg("plain")) {
      webServer.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Spotify rejected token\"}");
    } else {
      webServer.send(400, "text/html; charset=utf-8", "<!DOCTYPE html><html><head><meta charset='utf-8'></head><body><h1 style='color:red;'>Update Failed</h1><p>Spotify rejected this refresh token. Check Telnet/Serial logs.</p><a href='/'>Try again</a></body></html>");
    }
  }
}
void connectWifi() { LOG("[Main] Connecting to Wi-Fi..."); WiFi.begin(ssid, pass); unsigned long start = millis(); while (WiFi.status() != WL_CONNECTED && millis() - start < 30000) { delay(500); Serial.print('.'); } if (WiFi.status() == WL_CONNECTED) { LOG("\n[Main] Wi-Fi connected: " + WiFi.localIP().toString()); } else { LOG("\n[Main] Wi-Fi FAILED"); } }
void ensureWifiConnected() { static unsigned long lastTry = 0; if (WiFi.status() == WL_CONNECTED) return; unsigned long now = millis(); if (now - lastTry > 10000) { LOG("[Main] Wi-Fi lost - retrying connection to " + String(ssid) + "..."); WiFi.disconnect(); WiFi.begin(ssid, pass); lastTry = now; } }
void logError(const String& msg, int code) { LOG("[Error] " + msg + " (HTTP " + String(code) + ")"); }


// --- NEW: NDEF Tag Reading Logic ---
// This function completely replaces the old readNFCTag, readFromCard, and authenticateBlock functions.
void readNFCTag() {
    digitalWrite(TFT_CS, HIGH); // Ensure TFT is deselected on shared SPI bus
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
  LOG("[Main] playSpotifyUri -> " + uri);
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
    LOG("[Main] couldn't get now-playing (HTTP " + String(now.httpCode) + ")");
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
  lastJpgCount = count;
  
  // 4) Decode and Render
  digitalWrite(SS_PIN, HIGH); // Deselect MFRC522 while TFT draws on SPI bus
  tft.fillScreen(ILI9341_BLACK);
  JpegDec.abort();
  if (!JpegDec.decodeArray(jpgBuf, count)) {
    LOG("[Main] JPEG decode failed");
    return;
  }

  renderJPEG(0, -30);
  lastScreenActivityMillis = millis();
}

void showLastImage() {
  if (lastJpgCount > 0) {
    LOG("[Display] Rendering cached album art from RAM (" + String(lastJpgCount) + " bytes)...");
    digitalWrite(SS_PIN, HIGH); // Deselect MFRC522 while TFT draws on SPI bus
    tft.fillScreen(ILI9341_BLACK);
    JpegDec.abort();
    if (JpegDec.decodeArray(jpgBuf, lastJpgCount)) {
      renderJPEG(0, -30);
      lastScreenActivityMillis = millis();
      LOG("[Display] Cached album art displayed successfully");
      return;
    } else {
      LOG("[Display] Cached JPEG decode failed, falling back to Spotify API...");
    }
  } else {
    LOG("[Display] No cached image in RAM, fetching current album art from Spotify...");
  }
  showAlbumArt();
}

void showDeviceInfoScreen() {
  digitalWrite(SS_PIN, HIGH); // Deselect MFRC522 while TFT draws on SPI bus
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_GREEN);
  tft.setTextSize(2);
  tft.setCursor(15, 35);
  tft.println("Spotify RFID Player");
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setCursor(15, 75);
  tft.println("IP: " + WiFi.localIP().toString());
  tft.setTextSize(1);
  tft.setCursor(15, 115);
  tft.println("Web: http://spotify-player.local/");
  tft.setCursor(15, 135);
  tft.println("WiFi: " + String(ssid) + " (" + String(WiFi.RSSI()) + " dBm)");
  tft.setCursor(15, 165);
  tft.setTextColor(ILI9341_CYAN);
  tft.println("Ready - Tap RFID card to play");
  lastScreenActivityMillis = millis();
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
