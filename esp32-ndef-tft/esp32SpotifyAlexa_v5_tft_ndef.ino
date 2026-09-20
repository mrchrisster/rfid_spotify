#include <Arduino.h>
#include "DeviceConfig.h"
#if PLAYER_HAS_DISPLAY
#include <Adafruit_ILI9341.h>
#include <JPEGDecoder.h>
#endif
#include <WiFi.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <vector>
#include "RfidReader.h"
#include "RfidPresence.h"
#include "RfidRecovery.h"
#include "Reliability.h"
#include "SpotifyClient.h"
#include "DeviceAuth.h"
#include "DeviceIdentity.h"
#include "settings.h"

constexpr uint8_t TFT_CS = 15, TFT_DC = 2, TFT_RST = 22, SS_PIN = 5, RST_PIN = 4;
constexpr size_t MAX_JPEG = 64 * 1024;
const char firmwareBuild[] = __DATE__ " " __TIME__;
#if PLAYER_HAS_DISPLAY
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
#endif
MFRC522 reader(SS_PIN, MFRC522::UNUSED_PIN);
RfidPresence cardPresence;
SpotifyClient spotify(clientId, clientSecret, deviceName, refreshToken);
Preferences preferences;
WebServer webServer(80);
String adminPassword;

// Queues transfer POD values or explicitly owned buffers, never Arduino String objects.
enum class Command : uint8_t { Play, Next, Toggle, VolumeUp, VolumeDown, Devices, Select, Token, Refresh, Status };
struct Job { Command command; uint32_t id = 0; uint8_t attempt = 0; char value[1025] = {}; char name[128] = {}; };
struct Result {
  Command command; uint32_t id; int code;
  bool tokenValid, revoked, unsaved; uint32_t retryMs;
  char name[128], device[96];
  char* payload = nullptr;
  uint8_t* image = nullptr; size_t imageSize = 0;
};
enum class DisplayAction : uint8_t { Image, Cached, Clear, Info, Reset };
struct DisplayJob { DisplayAction action; uint8_t* image = nullptr; size_t size = 0; };
struct LogLine { char text[224]; };
QueueHandle_t jobs, results, displayJobs, logs;
std::atomic<uint32_t> nextJobId{1}, scans{0}, readFailures{0}, recoveries{0}, maxPollGap{0}, lastPoll{0}, rfidVersion{0}, droppedLogs{0};
std::atomic<bool> rfidOk{false};
TaskHandle_t networkTaskHandle, hardwareTaskHandle;
String logHistory[48]; size_t logHead = 0, logCount = 0;
String devicesJson = "{\"devices\":[]}";
struct Status { bool tokenValid = false, revoked = false, unsaved = false; uint32_t retryMs = 0, at = 0; String name, device; } status;
struct JobStatus { uint32_t id = 0; int code = 202; } jobStatus[8];

const char* resetReasonName() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on/chip reset";
    case ESP_RST_EXT: return "external reset";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "panic/exception";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep wake";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "SDIO reset";
    case ESP_RST_USB: return "USB reset";
    case ESP_RST_JTAG: return "JTAG reset";
    case ESP_RST_EFUSE: return "eFuse error";
    case ESP_RST_PWR_GLITCH: return "power glitch";
    case ESP_RST_CPU_LOCKUP: return "CPU lockup";
    default: return "unknown";
  }
}

void logMessage(const String& message) {
  LogLine line{};
  snprintf(line.text, sizeof(line.text), "[%lu ms] %s", (unsigned long)millis(), message.c_str());
  if (!logs || xQueueSend(logs, &line, 0) != pdTRUE) ++droppedLogs;
}
bool saveToken(const String& token, bool pkce) {
  // One NVS record commits token + grant type together, including after refresh rotation.
  JsonDocument doc; doc["token"] = token; doc["pkce"] = pkce;
  String data; serializeJson(doc, data);
  return preferences.putString("auth_v2", data) == data.length();
}
bool ok(int code) { return code == 200 || code == 204; }
void rememberJob(uint32_t id, int code) { if (id) jobStatus[id % 8] = {id, code}; }
bool submit(Job& job) {
  job.id = nextJobId.fetch_add(1);
  bool sent = xQueueSend(jobs, &job, 0) == pdTRUE;
  if (!sent) logMessage("[Queue] Busy; command rejected");
  return sent;
}
void publish(const Job& job, int code, const String& payload = "", uint8_t* image = nullptr, size_t imageSize = 0) {
  Result result{};
  result.command = job.command; result.id = job.id; result.code = code;
  result.unsaved = spotify.HasUnsavedToken(); result.tokenValid = spotify.IsTokenValid(); result.revoked = spotify.IsRevoked(); result.retryMs = spotify.RetryInMs();
  strlcpy(result.name, spotify.DeviceName().c_str(), sizeof(result.name));
  strlcpy(result.device, spotify.DeviceId().c_str(), sizeof(result.device));
  if (!payload.isEmpty()) result.payload = strdup(payload.c_str());
  result.image = image; result.imageSize = imageSize;
  if (xQueueSend(results, &result, portMAX_DELAY) != pdTRUE) {
    free(result.payload); free(image); logMessage("[Queue] Result dropped; inspect device status");
  }
}

// Spotify state/NVS writes belong to this task. TLS identity uses a separate
// namespace, owned exclusively by the Arduino task.
int playerState(JsonDocument& doc) {
  if (spotify.DeviceId().isEmpty() && spotify.GetDevices().isEmpty()) return 404;
  HttpResult response = spotify.CallAPI("GET", "https://api.spotify.com/v1/me/player");
  if (response.httpCode != 200) return response.httpCode == 204 ? 404 : response.httpCode;
  if (deserializeJson(doc, response.payload)) return 502;
  if (doc["device"]["id"] != spotify.DeviceId() || doc["device"]["is_restricted"].as<bool>()) return 409;
  return 200;
}
int control(Command command) {
  if (command == Command::Next) return spotify.Next();
  JsonDocument state;
  int code = playerState(state); if (code != 200) return code;
  String base = "https://api.spotify.com/v1/me/player/";
  if (command == Command::Toggle) {
    return spotify.CallAPI("PUT", base + (state["is_playing"].as<bool>() ? "pause" : "play") + "?device_id=" + spotify.DeviceId()).httpCode;
  }
  if (!state["device"]["supports_volume"].as<bool>() || !state["device"]["volume_percent"].is<int>()) return 403;
  int volume = constrain(state["device"]["volume_percent"].as<int>() + (command == Command::VolumeUp ? 10 : -10), 0, 100);
  return spotify.CallAPI("PUT", base + "volume?volume_percent=" + String(volume) + "&device_id=" + spotify.DeviceId()).httpCode;
}

class AlbumSelection {
  String artist;
  AlbumOrder order;
public:
  int choose(const String& uri, String& album) {
    String requested = uri.substring(15);
    if (artist != requested || order.empty()) {
      HttpResult response = spotify.CallAPI("GET", "https://api.spotify.com/v1/artists/" + requested + "/albums?include_groups=album,single&limit=1");
      if (response.httpCode != 200) return response.httpCode;
      JsonDocument doc;
      if (deserializeJson(doc, response.payload) || !doc["total"].is<unsigned int>()) return 502;
      unsigned total = doc["total"].as<unsigned>();
      if (!total) return 404;
      if (total > 5000) return 413;
      unsigned count = total, start = 0;
      if ((requested == "1l6d0RIxTL3JytlLGvWzYe" || requested == "3t2iKODSDyzoDJw7AsD99u") && total > 60) { count = 60; start = total - 60; }
      std::vector<uint16_t> replacement(count);
      for (unsigned i = 0; i < count; ++i) replacement[i] = start + i;
      for (size_t i = count; i > 1; --i) std::swap(replacement[i - 1], replacement[esp_random() % i]);
      order.commit(replacement); artist = requested;
    }
    if (order.empty()) return 404;
    if (order.exhausted()) order.reshuffle([] { return esp_random(); });
    uint16_t index;
    if (!order.current(index)) return 404;
    HttpResult response = spotify.CallAPI("GET", "https://api.spotify.com/v1/artists/" + requested + "/albums?include_groups=album,single&limit=1&offset=" + String(index));
    if (response.httpCode != 200) return response.httpCode;
    JsonDocument doc;
    if (deserializeJson(doc, response.payload)) return 502;
    album = doc["items"][0]["uri"] | "";
    char checked[SafeNdef::MaxUri];
    if (!album.startsWith("spotify:album:") || !SafeNdef::normalize(album.c_str(), checked, sizeof(checked))) {
      // Catalog changed; rebuild on the next scan rather than staying stuck on a removed offset.
      order.clear(); artist = ""; return 404;
    }
    return 200;
  }
  void played() { order.played(); }
};

void fetchArt(const Job& job) {
#if PLAYER_HAS_DISPLAY
  String url;
  {
    HttpResult response = spotify.CallAPI("GET", "https://api.spotify.com/v1/me/player/currently-playing");
    JsonDocument doc;
    if (response.httpCode != 200 || deserializeJson(doc, response.payload)) return;
    JsonArray images = doc["item"]["album"]["images"].as<JsonArray>();
    // Prefer an image that fits the display; handle albums with only one size.
    for (JsonObject image : images) {
      if (url.isEmpty() || image["width"].as<int>() >= 240) url = image["url"] | "";
    }
  }
  if (url.isEmpty() || heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < MAX_JPEG + 24 * 1024) return;
  auto* bytes = static_cast<uint8_t*>(malloc(MAX_JPEG));
  if (!bytes) return;
  int count = spotify.DownloadFile(url, bytes, MAX_JPEG);
  if (count < 4 || bytes[0] != 0xff || bytes[1] != 0xd8 || bytes[count - 2] != 0xff || bytes[count - 1] != 0xd9) {
    free(bytes); logMessage("[Art] Incomplete or unsupported image; retaining previous cover"); return;
  }
  uint8_t* smaller = static_cast<uint8_t*>(realloc(bytes, count));
  if (smaller) bytes = smaller;
  publish(job, 200, "", bytes, count);
#else
  (void)job;
#endif
}

void networkWorker(void*) {
  AlbumSelection albums;
  bool wasConnected = false, pending = false;
  Job retry{}; uint32_t retryAt = 0, maintenanceAt = millis() - 30000;
  for (;;) {
    bool connected = WiFi.status() == WL_CONNECTED;
    if (connected != wasConnected) { spotify.ResetState(); wasConnected = connected; }
    if (DeviceAuth::process(spotify)) {
      Job authStatus{}; authStatus.command = Command::Status; publish(authStatus, 200);
    }
    Job job{};
    bool received = xQueueReceive(jobs, &job, pdMS_TO_TICKS(50)) == pdTRUE;
    if (received && pending && (job.command == Command::Play || job.command == Command::Select)) {
      publish(retry, 409); pending = false; // New user intent supersedes old playback.
    }
    if (!received && pending && int32_t(millis() - retryAt) >= 0) { job = retry; received = true; pending = false; }
    if (!received) {
      if (millis() - maintenanceAt >= 30000) {
        maintenanceAt = millis();
        if (connected) spotify.EnsureTokenFresh();
        Job statusJob{}; statusJob.command = Command::Status; publish(statusJob, 200);
      }
      continue;
    }
    int code = 400; String payload;
    switch (job.command) {
      case Command::Token: code = spotify.ReplaceRefreshToken(job.value) ? 200 : spotify.LastError(); break;
      case Command::Refresh: code = spotify.EnsureTokenFresh(true) ? 200 : spotify.LastError(); break;
      case Command::Devices: {
        HttpResult response = spotify.CallAPI("GET", "https://api.spotify.com/v1/me/player/devices");
        code = response.httpCode;
        JsonDocument doc;
        if (code == 200 && !deserializeJson(doc, response.payload)) {
          JsonDocument filtered; JsonArray list = filtered["devices"].to<JsonArray>();
          for (JsonObject device : doc["devices"].as<JsonArray>()) {
            if (list.size() >= 16) break;
            if (!device["id"].is<const char*>() || device["is_restricted"].as<bool>()) continue;
            JsonObject item = list.add<JsonObject>();
            item["id"] = device["id"]; item["name"] = device["name"]; item["type"] = device["type"];
          }
          serializeJson(filtered, payload);
        } else if (code == 200) code = 502;
        break;
      }
      case Command::Select: {
        // Validate against live discovery; never trust a submitted id/name pair.
        HttpResult response = spotify.CallAPI("GET", "https://api.spotify.com/v1/me/player/devices");
        JsonDocument devices;
        code = response.httpCode;
        bool found = false;
        if (code == 200 && !deserializeJson(devices, response.payload)) {
          for (JsonObject device : devices["devices"].as<JsonArray>())
            if (device["id"] == job.value && device["name"] == job.name && !device["is_restricted"].as<bool>()) found = true;
          code = found ? 200 : 404;
        } else if (code == 200) code = 502;
        if (!found) break;
        JsonDocument transfer; transfer["device_ids"].to<JsonArray>().add(job.value); transfer["play"] = true;
        String body; serializeJson(transfer, body);
        code = spotify.CallAPI("PUT", "https://api.spotify.com/v1/me/player", body).httpCode;
        if (ok(code)) {
          spotify.SelectDevice(job.name, job.value);
          if (preferences.putString("device_name", job.name) != strlen(job.name)) code = 507;
        }
        break;
      }
      case Command::Play: {
        String uri = job.value;
        bool artist = uri.startsWith("spotify:artist:");
        if (artist) { code = albums.choose(uri, uri); if (code != 200) break; }
        if (spotify.DeviceId().isEmpty() && spotify.GetDevices().isEmpty()) { code = spotify.LastError() == 200 ? 404 : spotify.LastError(); break; }
        code = spotify.CallAPI("PUT", "https://api.spotify.com/v1/me/player/shuffle?state=false&device_id=" + spotify.DeviceId()).httpCode;
        if (!ok(code)) break;
        code = spotify.Play(uri);
        if (ok(code) && artist) albums.played();
        break;
      }
      case Command::Next: case Command::Toggle: case Command::VolumeUp: case Command::VolumeDown:
        code = control(job.command); break;
      default: break;
    }
    // Token contents never enter logs/results. Remove the queued copy promptly.
    if (job.command == Command::Token) memset(job.value, 0, sizeof(job.value));
    if (job.command == Command::Play && !ok(code) && job.attempt < 2 && (code < 0 || code == 404 || code == 429 || code >= 500)) {
      if (pending) publish(retry, 409);
      retry = job; ++retry.attempt; retryAt = millis() + max(2000UL, (unsigned long)spotify.RetryInMs()); pending = true;
      publish(job, 202); logMessage("[Playback] Retry scheduled (code " + String(code) + ")");
    } else {
      publish(job, code, payload);
      logMessage("[Job " + String(job.id) + "] completed (code " + String(code) + ")");
      if (job.command == Command::Play && ok(code) && uxQueueMessagesWaiting(jobs) == 0) fetchArt(job);
    }
  }
}

RfidRecovery::Registers readerRegisters() {
  return {reader.PCD_ReadRegister(MFRC522::VersionReg), reader.PCD_ReadRegister(MFRC522::TxControlReg),
    reader.PCD_ReadRegister(MFRC522::TModeReg), reader.PCD_ReadRegister(MFRC522::TPrescalerReg),
    reader.PCD_ReadRegister(MFRC522::CommandReg)};
}
void logReaderRegisters(const char* label, const RfidRecovery::Registers& state) {
  char line[176];
  snprintf(line,sizeof(line),"[RFID] %s: version=0x%02X antenna=0x%02X timer=0x%02X prescaler=0x%02X command=0x%02X reset_pin=%d",
    label,state.version,state.antenna,state.timer,state.prescaler,state.command,digitalRead(RST_PIN));
  logMessage(line);
}
struct ReaderResetPins {
  void output(uint8_t pin) { pinMode(pin,OUTPUT); }
  void write(uint8_t pin, bool high) { digitalWrite(pin,high ? HIGH : LOW); }
  void wait(unsigned ms) { delay(ms); }
};
bool readerHealth(bool force) {
  digitalWrite(TFT_CS,HIGH);
  auto registers=readerRegisters();
  if (!force && !registers.healthy()) { delay(2); registers=readerRegisters(); }
  if (force || !registers.healthy()) {
    cardPresence.uncertain(); // Retain held-card identity across reset/recovery.
    ++recoveries;
    logReaderRegisters("Before recovery",registers);
    digitalWrite(SS_PIN,HIGH);
    ReaderResetPins pins;
    RfidRecovery::reset(reader,pins,SS_PIN,RST_PIN);
    registers=readerRegisters();
    logReaderRegisters(registers.healthy() ? "Reader initialized" : "Hardware recovery failed",registers);
  }
  rfidVersion=registers.version; rfidOk=registers.healthy(); return registers.healthy();
}
void pollCard() {
  static uint8_t errors = 0;
  uint32_t now = millis(), previousPoll = lastPoll.exchange(now);
  if (previousPoll && now - previousPoll > maxPollGap) maxPollGap = now - previousPoll;
  if (!rfidOk) { cardPresence.uncertain(); return; }
  digitalWrite(TFT_CS, HIGH);
  reader.PCD_StopCrypto1();
  uint8_t atqa[2], atqaSize = sizeof(atqa);
  auto response = reader.PICC_WakeupA(atqa, &atqaSize);
  if (response == MFRC522::STATUS_TIMEOUT) {
    bool healthy=readerRegisters().healthy();
    if (!healthy) healthy=readerHealth(false);
    if (cardPresence.missing(millis(),healthy)) logMessage("[RFID] Card removed; ready for next presentation");
    errors=0; return;
  }
  cardPresence.uncertain();
  if ((response != MFRC522::STATUS_OK && response != MFRC522::STATUS_COLLISION) || !reader.PICC_ReadCardSerial()) {
    ++readFailures; reader.PCD_StopCrypto1();
    if (++errors >= 3) { readerHealth(true); errors = 0; }
    return;
  }
  if (!cardPresence.seen(reader.uid.uidByte,reader.uid.size)) {
    reader.PICC_HaltA(); reader.PCD_StopCrypto1(); errors=0; return;
  }
  ++scans;
  char uri[SafeNdef::MaxUri] = {};
  RfidReader tag(reader); bool valid = tag.spotifyUri(uri, sizeof(uri));
  reader.PICC_HaltA(); reader.PCD_StopCrypto1();
  if (!valid) {
    ++readFailures;
    logMessage(tag.ioError ? "[RFID] Card read failed; remove and retry" : "[RFID] Unsupported or malformed Spotify NDEF record");
    if (tag.ioError && ++errors >= 3) { readerHealth(true); errors = 0; }
    return;
  }
  errors = 0;
  Job job{}; job.command = Command::Play; strlcpy(job.value, uri, sizeof(job.value));
  if (submit(job)) logMessage("[RFID] Queued " + String(uri) + " as job " + String(job.id));
}
#if PLAYER_HAS_DISPLAY
void infoScreen() {
  digitalWrite(SS_PIN, HIGH); tft.fillScreen(ILI9341_BLACK); tft.setTextColor(ILI9341_GREEN); tft.setTextSize(2); tft.setCursor(12, 30);
  tft.println("Spotify RFID Player"); tft.setTextColor(ILI9341_WHITE); tft.setTextSize(1); tft.setCursor(12, 80);
  tft.println("IP: " + WiFi.localIP().toString()); tft.setCursor(12, 105); tft.println("http://" + String(DeviceAuth::hostname()) + ".local/");
  tft.setCursor(12, 140); tft.println(PLAYER_REQUIRE_WEB_AUTH ? "Admin login: see USB Serial at boot" : "Web UI: no login required");
}
bool decodeCover(uint8_t* bytes, size_t size, bool draw) {
  JpegDec.abort();
  if (!JpegDec.decodeArray(bytes, size) || JpegDec.width > 640 || JpegDec.height > 640) { JpegDec.abort(); return false; }
  int expected = JpegDec.MCUSPerRow * JpegDec.MCUSPerCol, count = 0;
  if (draw) { digitalWrite(SS_PIN, HIGH); tft.fillScreen(ILI9341_BLACK); }
  while (JpegDec.read()) {
    ++count;
    if (draw) {
      int x = JpegDec.MCUx * JpegDec.MCUWidth, y = JpegDec.MCUy * JpegDec.MCUHeight - 30;
      digitalWrite(SS_PIN, HIGH);
      // One SPI transaction per MCU, rather than one per pixel.
      tft.drawRGBBitmap(x, y, JpegDec.pImage, JpegDec.MCUWidth, JpegDec.MCUHeight);
    }
    if (millis() - lastPoll >= 100) pollCard();
    taskYIELD();
  }
  JpegDec.abort(); return count == expected;
}
#endif
void hardwareWorker(void*) {
  pinMode(SS_PIN, OUTPUT); digitalWrite(SS_PIN, HIGH);
  pinMode(TFT_CS, OUTPUT); digitalWrite(TFT_CS, HIGH); pinMode(RST_PIN, OUTPUT);
  SPI.begin(18, 19, 23);
#if PLAYER_HAS_DISPLAY
  tft.begin(); tft.setRotation(1); infoScreen();
  logMessage("[Display] TFT enabled; initialization and info-screen draw completed");
  uint8_t* cachedImage = nullptr; size_t cachedSize = 0;
  uint32_t screenAt = millis();
#endif
  readerHealth(true);
  uint32_t lastHealth = millis();
  for (;;) {
    if (millis() - lastPoll >= 100) pollCard();
    if (millis() - lastHealth >= (rfidOk ? 30000UL : 5000UL)) { readerHealth(false); lastHealth = millis(); }
    DisplayJob job{};
    if (xQueueReceive(displayJobs, &job, 0) == pdTRUE) {
      switch (job.action) {
#if PLAYER_HAS_DISPLAY
        case DisplayAction::Image:
          if (decodeCover(job.image, job.size, false)) {
            free(cachedImage); cachedImage = job.image; cachedSize = job.size;
            decodeCover(cachedImage, cachedSize, true); screenAt = millis();
          } else { free(job.image); logMessage("[Art] JPEG invalid; previous cover retained"); }
          break;
        case DisplayAction::Cached: if (cachedImage) { decodeCover(cachedImage, cachedSize, true); screenAt = millis(); } break;
        case DisplayAction::Info: infoScreen(); screenAt = millis(); break;
        case DisplayAction::Clear: digitalWrite(SS_PIN, HIGH); tft.fillScreen(ILI9341_BLACK); screenAt = 0; break;
#else
        case DisplayAction::Image: free(job.image); break;
        case DisplayAction::Cached: case DisplayAction::Info: case DisplayAction::Clear: break;
#endif
        case DisplayAction::Reset: readerHealth(true); break;
      }
    }
#if PLAYER_HAS_DISPLAY
    if (screenAt && millis() - screenAt > 600000UL) { digitalWrite(SS_PIN, HIGH); tft.fillScreen(ILI9341_BLACK); screenAt = 0; }
#endif
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

#include "Dashboard.h"
bool authorized(bool mutation = false) {
#if PLAYER_REQUIRE_WEB_AUTH
  if (!webServer.authenticate("admin", adminPassword.c_str())) {
    webServer.requestAuthentication(DIGEST_AUTH, "Spotify RFID", "Authentication required"); return false;
  }
  #endif
  if (mutation && webServer.header("X-Requested-With") != "RFIDPlayer") {
    webServer.send(403, "application/json", "{\"message\":\"Missing request header\"}"); return false;
  }
  webServer.sendHeader("Cache-Control", "no-store");
  webServer.sendHeader("X-Content-Type-Options", "nosniff");
  webServer.sendHeader("X-Frame-Options", "DENY");
  return true;
}
void handleCommand(Command command) {
  if (!authorized(true)) return;
  Job job{}; job.command = command;
  if (command == Command::Token || command == Command::Select) {
    String body = webServer.arg("plain"); JsonDocument doc;
    if (body.length() > 1536 || deserializeJson(doc, body)) { webServer.send(400, "application/json", "{\"message\":\"Invalid JSON\"}"); return; }
    String value = doc[command == Command::Token ? "token" : "id"] | ""; value.trim();
    String name = doc["name"] | "";
    if (value.isEmpty() || value.length() >= sizeof(job.value) || (command == Command::Select && (value.length() >= 96 || name.isEmpty() || name.length() >= sizeof(job.name)))) {
      webServer.send(400, "application/json", "{\"message\":\"Missing or oversized value\"}"); return;
    }
    strlcpy(job.value, value.c_str(), sizeof(job.value)); strlcpy(job.name, name.c_str(), sizeof(job.name));
  }
  if (!submit(job)) { webServer.send(503, "application/json", "{\"message\":\"Command queue full; try again\"}"); return; }
  rememberJob(job.id, 202);
  webServer.send(202, "application/json", "{\"job\":" + String(job.id) + "}");
}
void handleDisplay(DisplayAction action) {
  if (!authorized(true)) return;
#if !PLAYER_HAS_DISPLAY
  if (action != DisplayAction::Reset) {
    webServer.send(409, "application/json", "{\"message\":\"This player has no display\"}"); return;
  }
#endif
  DisplayJob job{}; job.action = action;
  bool sent = xQueueSend(displayJobs, &job, 0) == pdTRUE;
  webServer.send(sent ? 202 : 503, "application/json", sent ? "{\"message\":\"Display/reader request queued\"}" : "{\"message\":\"Hardware queue full\"}");
}
void handleStatus() {
  if (!authorized()) return;
  JsonDocument doc;
  doc["firmware_build"] = firmwareBuild;
  doc["has_display"] = bool(PLAYER_HAS_DISPLAY);
  doc["web_auth_required"] = bool(PLAYER_REQUIRE_WEB_AUTH);
  doc["reset_reason"] = int(esp_reset_reason()); doc["reset_reason_name"] = resetReasonName();
  doc["certificate_days"] = DeviceAuth::certificateDays();
  doc["certificate_automatic"] = DeviceIdentity::automatic();
  doc["certificate_renewal"] = DeviceIdentity::renewalStatus();
  doc["certificate_issuer_days"] = DeviceIdentity::issuerDays();
  doc["reconnect_ready"] = DeviceAuth::ready(); doc["reconnect_url"] = DeviceAuth::url();
  doc["uptime_seconds"] = esp_timer_get_time() / 1000000ULL;
  doc["free_heap"] = ESP.getFreeHeap(); doc["min_heap"] = ESP.getMinFreeHeap();
  doc["largest_heap"] = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  doc["wifi_rssi"] = WiFi.RSSI(); doc["ip"] = WiFi.localIP().toString();
  doc["device_name"] = status.name; doc["device_id"] = status.device;
  doc["token_unsaved"] = status.unsaved;
  doc["token_valid"] = status.tokenValid; doc["revoked"] = status.revoked;
  uint32_t elapsed = millis() - status.at;
  doc["retry_ms"] = elapsed < status.retryMs ? status.retryMs - elapsed : 0;
  doc["rfid_ok"] = rfidOk.load(); doc["rfid_version"] = String(rfidVersion.load(), HEX);
  doc["scans"] = scans.load(); doc["read_failures"] = readFailures.load(); doc["recoveries"] = recoveries.load();
  doc["max_poll_gap_ms"] = maxPollGap.load(); doc["poll_age_ms"] = millis() - lastPoll.load(); doc["dropped_logs"] = droppedLogs.load();
  doc["network_stack_free"] = uxTaskGetStackHighWaterMark(networkTaskHandle);
  doc["hardware_stack_free"] = uxTaskGetStackHighWaterMark(hardwareTaskHandle);
  JsonArray recent = doc["jobs"].to<JsonArray>();
  for (const auto& job : jobStatus) if (job.id) { JsonObject entry = recent.add<JsonObject>(); entry["id"] = job.id; entry["code"] = job.code; }
  String out; serializeJson(doc, out); webServer.send(200, "application/json", out);
}
void setupWeb() {
  const char* headers[] = {"X-Requested-With"}; webServer.collectHeaders(headers, 1);
  webServer.on("/", HTTP_GET, [] { if (authorized()) webServer.send_P(200, "text/html; charset=utf-8", dashboard); });
  webServer.on("/api/status", HTTP_GET, handleStatus);
  webServer.on("/api/devices", HTTP_GET, [] { if (authorized()) webServer.send(200, "application/json", devicesJson); });
  webServer.on("/api/logs", HTTP_GET, [] {
    if (!authorized()) return;
    String out; out.reserve(48 * 224);
    for (size_t i = 0; i < logCount; ++i) out += logHistory[(logHead + 48 - logCount + i) % 48] + '\n';
    webServer.send(200, "text/plain", out);
  });
  webServer.on("/api/token", HTTP_POST, [] { handleCommand(Command::Token); });
  webServer.on("/update", HTTP_POST, [] { handleCommand(Command::Token); });
  webServer.on("/api/refresh_token", HTTP_POST, [] { handleCommand(Command::Refresh); });
  webServer.on("/api/rescan", HTTP_POST, [] { handleCommand(Command::Devices); });
  webServer.on("/api/select_device", HTTP_POST, [] { handleCommand(Command::Select); });
  webServer.on("/api/next", HTTP_POST, [] { handleCommand(Command::Next); });
  webServer.on("/api/playpause", HTTP_POST, [] { handleCommand(Command::Toggle); });
  webServer.on("/api/volup", HTTP_POST, [] { handleCommand(Command::VolumeUp); });
  webServer.on("/api/voldown", HTTP_POST, [] { handleCommand(Command::VolumeDown); });
  webServer.on("/api/reset_rfid", HTTP_POST, [] { handleDisplay(DisplayAction::Reset); });
  webServer.on("/api/show_info", HTTP_POST, [] { handleDisplay(DisplayAction::Info); });
  webServer.on("/api/show_image", HTTP_POST, [] { handleDisplay(DisplayAction::Cached); });
  webServer.on("/api/clear_screen", HTTP_POST, [] { handleDisplay(DisplayAction::Clear); });
  webServer.on("/api/restart", HTTP_POST, [] {
    if (!authorized(true)) return;
    webServer.send(200, "application/json", "{\"message\":\"Restarting\"}"); delay(100); ESP.restart();
  });
  webServer.onNotFound([] { webServer.send(404, "application/json", "{\"message\":\"Not found\"}"); });
  webServer.begin();
}
void setup() {
  Serial.begin(115200);
  logs = xQueueCreate(24, sizeof(LogLine)); jobs = xQueueCreate(4, sizeof(Job));
  results = xQueueCreate(8, sizeof(Result)); displayJobs = xQueueCreate(3, sizeof(DisplayJob));
  if (!logs || !jobs || !results || !displayJobs || !preferences.begin("spotify", false)) {
    Serial.println("Fatal: cannot allocate queues/open NVS"); while (true) delay(1000);
  }
  String saved; bool savedPkce = false;
  String authRecord = preferences.getString("auth_v2", "");
  if (!authRecord.isEmpty()) {
    JsonDocument doc;
    if (deserializeJson(doc, authRecord) || !doc["token"].is<const char*>() || !doc["pkce"].is<bool>()) {
      Serial.println("WARNING: invalid stored authentication; reconnect through the web UI");
    } else { saved = doc["token"].as<String>(); savedPkce = doc["pkce"].as<bool>(); }
  } else {
    saved = preferences.getString("ref_token", "");
    if (saved.isEmpty()) saved = refreshToken;
    if (!saved.isEmpty() && !saveToken(saved, false)) Serial.println("WARNING: bootstrap token could not be saved");
  }
  spotify.SetRefreshToken(saved, savedPkce); spotify.SetRefreshTokenCallback(saveToken);
  spotify.SelectDevice(preferences.getString("device_name", deviceName)); status.name = spotify.DeviceName();
  #if PLAYER_REQUIRE_WEB_AUTH
  adminPassword = preferences.getString("admin_pass", "");
  if (adminPassword.isEmpty()) {
    char randomPassword[33];
    for (int i = 0; i < 4; ++i) snprintf(randomPassword + 8 * i, 9, "%08lx", (unsigned long)esp_random());
    adminPassword = randomPassword;
    if (preferences.putString("admin_pass", adminPassword) != adminPassword.length()) {
      Serial.println("Fatal: cannot persist admin password"); while (true) delay(1000);
    }
  }
  // Deliberately USB-only: never add this password to web logs.
  Serial.println("Admin username: admin; password: " + adminPassword);
  #else
  Serial.println("Web UI: login disabled (internal LAN)");
  #endif
  logMessage("[Boot] Build " + String(firmwareBuild) + "; TFT=" + String(PLAYER_HAS_DISPLAY));
  logMessage("[Boot] Reset reason " + String(esp_reset_reason()) + " (" + resetReasonName() + ")");
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true); WiFi.begin(ssid, pass);
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  DeviceAuth::begin(clientId, adminPassword);
  setupWeb();
  if (xTaskCreate(hardwareWorker, "rfid-display", 8192, nullptr, 2, &hardwareTaskHandle) != pdPASS ||
      xTaskCreate(networkWorker, "spotify", 14336, nullptr, 1, &networkTaskHandle) != pdPASS) {
    Serial.println("Fatal: task creation failed"); while (true) delay(1000);
  }
}
void loop() {
  // HTTP/serial backpressure can delay the dashboard, but never RFID polling.
  webServer.handleClient();
  static bool wasConnected = false; static uint32_t retryAt = 0;
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wasConnected) {
    MDNS.end(); if (MDNS.begin(DeviceAuth::hostname())) MDNS.addService("http", "tcp", 80);
    webServer.close(); webServer.begin(); logMessage("[WiFi] Connected at " + WiFi.localIP().toString());
  }
  if (!connected && millis() - retryAt >= 15000) { retryAt = millis(); WiFi.reconnect(); }
  DeviceAuth::tick(connected);
  static bool httpsAdvertised = false;
  if (!connected) httpsAdvertised = false;
  if (connected && DeviceAuth::ready() && !httpsAdvertised) { MDNS.addService("https", "tcp", 443); httpsAdvertised = true; }
  wasConnected = connected;
  Result result{};
  while (xQueueReceive(results, &result, 0) == pdTRUE) {
    status.unsaved = result.unsaved; status.tokenValid = result.tokenValid; status.revoked = result.revoked; status.retryMs = result.retryMs; status.at = millis();
    status.name = result.name; status.device = result.device;
    rememberJob(result.id, result.code);
    if (result.command == Command::Devices) devicesJson = result.code == 200 && result.payload ? result.payload : "{\"devices\":[]}";
    free(result.payload);
    if (result.image) {
      DisplayJob display{}; display.action = DisplayAction::Image; display.image = result.image; display.size = result.imageSize;
      if (xQueueSend(displayJobs, &display, 0) != pdTRUE) free(result.image);
    }
  }
  LogLine line{};
  for (int i = 0; i < 8 && xQueueReceive(logs, &line, 0) == pdTRUE; ++i) {
    logHistory[logHead] = line.text; logHead = (logHead + 1) % 48; if (logCount < 48) ++logCount;
    Serial.println(line.text);
  }
  delay(5);
}
