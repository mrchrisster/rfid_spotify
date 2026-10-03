#include <Arduino.h>
#include "DeviceConfig.h"
#include "DisplaySettings.h"
#include "Artwork.h"
#include "CoverLayout.h"
#include "StartupPolicy.h"
#include "LoadingState.h"
#include "PlayerLanguage.h"
#include "WifiSetup.h"
#if PLAYER_HAS_DISPLAY
#include <Adafruit_ILI9341.h>
#include <JPEGDecoder.h>
// JPEGDecoder aliases SPIFFS to LittleFS; we pass an explicit SPIFFS File handle.
#ifdef SPIFFS
#undef SPIFFS
#endif
#include "PlayerScreen.h"
#include "LoadingScreen.h"
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
#include "ArtworkSpool.h"
#include "DeviceAuth.h"
#include "DeviceIdentity.h"
#include "settings.h"

constexpr uint8_t TFT_CS = 15, TFT_DC = 2, TFT_RST = 22, SS_PIN = 5, RST_PIN = 4;
const char firmwareBuild[] = __DATE__ " " __TIME__;
#if PLAYER_HAS_DISPLAY
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
#endif
MFRC522 reader(SS_PIN, MFRC522::UNUSED_PIN);
RfidPresence cardPresence;
SpotifyClient spotify(clientId, clientSecret, deviceName, refreshToken);
Preferences preferences;
Preferences uiPreferences; // Owned only by setup/HTTP loop, never the network worker.
bool uiStorageReady = false;
std::atomic<uint8_t> playerLanguage{PlayerLanguage::Default};
std::atomic<uint32_t> displaySettings{DisplaySettings::DefaultSeconds};
#if PLAYER_HAS_DISPLAY
QueueHandle_t tftLogQueue=nullptr;
#endif
namespace WifiSetup { const char* languageCode() { return PlayerLanguage::code(playerLanguage.load()); } }
WebServer webServer(80);
String adminPassword;

// Queues transfer POD values or explicitly owned buffers, never Arduino String objects.
enum class Command : uint8_t { Play, Next, Toggle, VolumeUp, VolumeDown, Devices, Select, Token, Refresh, Artwork, Status };
struct Job { Command command; uint32_t id = 0; uint8_t attempt = 0; char value[1025] = {}; char name[128] = {}; };
struct Result {
  Command command; uint32_t id; int code;
  bool tokenValid, revoked, unsaved; uint32_t retryMs;
  char name[128], device[96];
  char* payload = nullptr;
  uint8_t* image = nullptr; size_t imageSize = 0; bool flashImage=false;
};
enum class DisplayAction : uint8_t { Image, Cached, Clear, Info, Reset, LoadingFailed };
struct DisplayJob { DisplayAction action; uint8_t* image = nullptr; size_t size = 0; bool flashImage=false; uint32_t jobId=0; uint8_t loadingError=3; };
struct LogLine { char text[224]; };
QueueHandle_t jobs, results, displayJobs, logs;
std::atomic<uint32_t> nextJobId{1}, scans{0}, readFailures{0}, recoveries{0}, maxPollGap{0}, lastPoll{0}, rfidVersion{0}, droppedLogs{0};
std::atomic<bool> rfidOk{false};
#if PLAYER_HAS_DISPLAY
// Bits: valid access token, login required, credential not yet persisted.
std::atomic<uint8_t> screenAuth{0};
LoadingState loading; // Hardware-worker owned, including calls from pollCard.
bool infoVisible=false;
#endif
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
void publish(const Job& job, int code, const String& payload = "", uint8_t* image = nullptr, size_t imageSize = 0, bool flashImage=false) {
  Result result{};
  result.command = job.command; result.id = job.id; result.code = code;
  result.unsaved = spotify.HasUnsavedToken(); result.tokenValid = spotify.IsTokenValid(); result.revoked = spotify.IsRevoked(); result.retryMs = spotify.RetryInMs();
  strlcpy(result.name, spotify.DeviceName().c_str(), sizeof(result.name));
  strlcpy(result.device, spotify.DeviceId().c_str(), sizeof(result.device));
  if (!payload.isEmpty()) result.payload = strdup(payload.c_str());
  result.image = image; result.imageSize = imageSize;result.flashImage=flashImage;
  if (xQueueSend(results, &result, portMAX_DELAY) != pdTRUE) {
    if(flashImage)ArtworkSpool::release();
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

bool fetchArt(const Job& job, const String& context) {
#if PLAYER_HAS_DISPLAY
  if(ArtworkSpool::busy.load()){logMessage("[Art] Previous cover is still being decoded");return false;}
  String endpoint=Artwork::metadataUrl(context);
  if(endpoint.isEmpty())endpoint="https://api.spotify.com/v1/me/player/currently-playing";
  String urls[4];int widths[4]={};size_t choices=0;
  {
    bool playbackFallback=false;
    HttpResult response=spotify.CallAPI("GET",endpoint);
    // Large audiobook album responses can exceed the bounded JSON body limit.
    // Fall back to the smaller playback response without increasing that limit.
    if(response.httpCode==502 && !Artwork::metadataUrl(context).isEmpty()) {
      playbackFallback=true;
      response=spotify.CallAPI("GET","https://api.spotify.com/v1/me/player/currently-playing");
    }
    if(response.httpCode!=200) {
      logMessage("[Art] Metadata unavailable (HTTP " + String(response.httpCode) + "); use Reload cover to retry");return false;
    }
    JsonDocument doc;
    if(deserializeJson(doc,response.payload)) {logMessage("[Art] Invalid metadata response");return false;}
    if(playbackFallback && !Artwork::matchesPlayback(doc,context)) {
      logMessage("[Art] Waiting for Spotify playback metadata to catch up");return false;
    }
    int ceiling=640;
    while(choices<4) {
      urls[choices]=Artwork::imageUrl(doc,ceiling,&widths[choices]);
      if(urls[choices].isEmpty())break;
      ceiling=widths[choices]-1;++choices;
    }
  }
  if(!choices) {logMessage("[Art] No supported cover in Spotify metadata");return false;}
  uint8_t* bytes=nullptr;
  for(size_t i=0;i<choices;++i) {
    logMessage("[Art] Downloading "+String(widths[i])+"px cover");
    int count=spotify.DownloadArtwork(urls[i],bytes,64*1024);
    if(!count && spotify.LastError()==413 && i+1<choices) {
      logMessage("[Art] Cover exceeds buffer; trying smaller image");continue;
    }
    if(count>0 && !bytes) {publish(job,200,"",nullptr,count,true);return true;}
    if(count<4 || bytes[0]!=0xff || bytes[1]!=0xd8 || bytes[count-2]!=0xff || bytes[count-1]!=0xd9) {
      free(bytes);logMessage("[Art] Download failed or unsupported JPEG (bytes=" + String(count) + ", code=" + String(spotify.LastError()) + ")");return false;
    }
    uint8_t* smaller=static_cast<uint8_t*>(realloc(bytes,count));if(smaller)bytes=smaller;
    logMessage("[Art] Downloaded " + String(count) + " bytes; queued for display");
    publish(job,200,"",bytes,count);return true;
  }
  free(bytes);return false;
#else
  (void)job;(void)context;return false;
#endif
}

void networkWorker(void*) {
  AlbumSelection albums;
  String lastPlayedContext;
  bool wasConnected = false, pending = false;
#if PLAYER_HAS_DISPLAY
  bool artPending=false;Job artJob{};uint8_t artAttempts=0;uint32_t artAt=0;
#endif
  Job retry{}; uint32_t retryAt = 0, maintenanceAt = millis() - 30000;
  for (;;) {
#if PLAYER_HAS_DISPLAY
    uint8_t auth = spotify.IsTokenValid() ? 1 : (spotify.IsRevoked() ? 2 : (screenAuth.load() & 2));
    if (spotify.HasUnsavedToken()) auth |= 4;
    screenAuth.store(auth);
#endif
    bool connected = WiFi.status() == WL_CONNECTED;
    if (connected != wasConnected) { spotify.ResetState(); wasConnected = connected;maintenanceAt=millis()-30000; }
    // Clock/Wi-Fi readiness must not consume the first 30-second refresh interval.
    bool clockReady=time(nullptr)>=1700000000;
    if(StartupPolicy::authDue(millis(),maintenanceAt,connected,clockReady,spotify.IsTokenValid(),spotify.RetryInMs())) {
      maintenanceAt=millis();spotify.EnsureTokenFresh();
      Job statusJob{};statusJob.command=Command::Status;publish(statusJob,200);
    }
    if (DeviceAuth::process(spotify)) {
      Job authStatus{}; authStatus.command = Command::Status; publish(authStatus, 200);
    }
    Job job{};
    bool received = xQueueReceive(jobs, &job, pdMS_TO_TICKS(50)) == pdTRUE;
    if (received && pending && (job.command == Command::Play || job.command == Command::Select)) {
      publish(retry, 409); pending = false; // New user intent supersedes old playback.
    }
    if (!received && pending && !StartupPolicy::waitForAuth(connected,clockReady,spotify.IsTokenValid(),spotify.IsRevoked(),spotify.LastError()) && int32_t(millis() - retryAt) >= 0) { job = retry; received = true; pending = false; }
    if (!received) {
#if PLAYER_HAS_DISPLAY
      if(artPending && connected && !spotify.RetryInMs() && uint32_t(millis()-artAt)>=1500) {
        bool loaded=fetchArt(artJob,lastPlayedContext);artAt=millis();
        artPending=!loaded && ++artAttempts<3;
        if(!loaded && !artPending){
          logMessage("[Art] Cover unavailable after retries; use Reload cover");
          DisplayJob failure{};failure.action=DisplayAction::LoadingFailed;failure.jobId=artJob.id;failure.loadingError=4;
          xQueueSend(displayJobs,&failure,0);
        }
      }
#endif
      continue;
    }
    if(job.command==Command::Play && StartupPolicy::waitForAuth(connected,clockReady,spotify.IsTokenValid(),spotify.IsRevoked(),spotify.LastError())) {
      retry=job;retryAt=millis();pending=true;publish(job,202);
      logMessage("[Playback] Card queued; waiting for Wi-Fi, clock and Spotify authorization");
      continue;
    }
    int code = 400; String payload;
    switch (job.command) {
      case Command::Token: code = spotify.ReplaceRefreshToken(job.value) ? 200 : spotify.LastError(); break;
      case Command::Refresh: code = spotify.EnsureTokenFresh(true) ? 200 : spotify.LastError(); break;
      case Command::Artwork: code=fetchArt(job,lastPlayedContext)?200:502;break;
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
        JsonDocument transfer; transfer["device_ids"].to<JsonArray>().add(job.value); transfer["play"] = false;
        String body; serializeJson(transfer, body);
        code = spotify.CallAPI("PUT", "https://api.spotify.com/v1/me/player", body).httpCode;
        if (ok(code)) {
          // Keep the old default in RAM as well if its replacement cannot be saved.
          if (preferences.putString("device_name", job.name) != strlen(job.name)) code = 507;
          else spotify.SelectDevice(job.name, job.value);
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
        if (ok(code)) lastPlayedContext=uri;
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
#if PLAYER_HAS_DISPLAY
      if (job.command == Command::Play && ok(code)) {
        artJob=job;artAttempts=0;artAt=millis()-1500;artPending=true;
      }
#endif
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
bool readerHealth(bool force, const char* reason) {
  static RfidRecovery::Cooldown cooldown;
  static bool failureReported = false;
  static uint32_t failureLogAt = 0;
  digitalWrite(TFT_CS,HIGH);
  auto registers=readerRegisters();
  if (!force && !registers.healthy()) { delay(2); registers=readerRegisters(); }
  if (force || !registers.healthy()) {
    cardPresence.uncertain(); // Retain held-card identity across reset/recovery.
    if (!cooldown.acquire(millis())) {
      // Pause polling until the scheduled health check instead of resetting in a loop.
#if PLAYER_RFID_DEBUG
      logMessage("[RFID] Recovery deferred (5s minimum interval): " + String(reason));
#endif
      rfidVersion=registers.version; rfidOk=false; return false;
    }
    ++recoveries;
#if PLAYER_RFID_DEBUG
    logMessage("[RFID] Recovery trigger: " + String(reason));
    logReaderRegisters("Before recovery",registers);
#endif
    digitalWrite(SS_PIN,HIGH);
    ReaderResetPins pins;
    RfidRecovery::reset(reader,pins,SS_PIN,RST_PIN);
    registers=readerRegisters();
#if PLAYER_RFID_DEBUG
    logReaderRegisters(registers.healthy() ? "Reader initialized" : "Hardware recovery failed",registers);
#endif
    if (registers.healthy()) {
      logMessage("[RFID] Reader ready (" + String(reason) + ")");
      failureReported = false;
    } else if (!failureReported || millis()-failureLogAt >= 30000) {
      logMessage("[RFID] Reader unavailable; retrying automatically (recovery count=" + String(recoveries.load()) + ")");
      failureReported = true; failureLogAt = millis();
    }
  } else if (failureReported) {
    logMessage("[RFID] Reader ready; communication restored");
    failureReported = false;
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
    if (!healthy) healthy=readerHealth(false, "register mismatch after RF timeout");
    if (cardPresence.missing(millis(),healthy)) logMessage("[RFID] Card removed; ready for next presentation");
    errors=0; return;
  }
  cardPresence.uncertain();
  const bool woke = response == MFRC522::STATUS_OK || response == MFRC522::STATUS_COLLISION;
  auto selection = woke ? reader.PICC_Select(&reader.uid) : response;
  if (!woke || selection != MFRC522::STATUS_OK) {
    ++readFailures; reader.PCD_StopCrypto1();
#if PLAYER_RFID_DEBUG
    logMessage("[RFID] " + String(woke ? "Card selection" : "Wakeup") + " failed: status=" +
      String(int(selection)) + " (" + String(MFRC522::GetStatusCodeName(selection)) + ")");
#endif
    if (++errors >= 3) { readerHealth(true, "three consecutive wakeup/selection/read errors"); errors = 0; }
    return;
  }
  if (!cardPresence.seen(reader.uid.uidByte,reader.uid.size)) {
    reader.PICC_HaltA(); reader.PCD_StopCrypto1(); errors=0; return;
  }
  ++scans;
#if PLAYER_HAS_DISPLAY
  loading.begin(millis());loading.job=nextJobId.fetch_add(1); // Reserve ordering even for malformed cards.
  if(!WifiSetup::active() && !DisplaySettings::debug(displaySettings.load())) {
    digitalWrite(SS_PIN,HIGH);infoVisible=false;
    LoadingScreen::show(tft,playerLanguage.load(),false);loading.drawn=true;
    digitalWrite(TFT_CS,HIGH);
  }
#endif
  char uri[SafeNdef::MaxUri] = {};
  RfidReader tag(reader); bool valid = tag.spotifyUri(uri, sizeof(uri));
  reader.PICC_HaltA(); reader.PCD_StopCrypto1();
  if (!valid) {
#if PLAYER_HAS_DISPLAY
    loading.fail(0,millis(),tag.ioError?2:1);
#endif
    ++readFailures;
    logMessage(tag.ioError ? "[RFID] Card read failed; remove and retry" : "[RFID] Unsupported or malformed Spotify NDEF record");
    if (tag.ioError) {
#if PLAYER_RFID_DEBUG
      logMessage("[RFID] " + String(tag.errorOperation) + " failed: address=" + String(tag.errorAddress) +
        " status=" + String(int(tag.lastStatus)) + " (" + String(MFRC522::GetStatusCodeName(tag.lastStatus)) +
        ") bytes=" + String(tag.responseBytes));
#endif
      if (++errors >= 3) { readerHealth(true, "three consecutive wakeup/selection/read errors"); errors = 0; }
    }
    return;
  }
  errors = 0;
  Job job{}; job.command = Command::Play; strlcpy(job.value, uri, sizeof(job.value));
  bool queued=submit(job);
#if PLAYER_HAS_DISPLAY
  loading.job=job.id;if(!queued)loading.fail(job.id,millis(),5);
#endif
  if (queued) logMessage("[RFID] Queued " + String(uri) + " as job " + String(job.id));
}
#if PLAYER_HAS_DISPLAY
uint32_t screenStatusAt = 0, screenIp = 0;
uint8_t screenState = 255, screenLanguage = 255, screenPortalState = 255;
bool showingWifiSetup = false;
void updateInfoScreen(bool force = false) {
  if (!infoVisible || (!force && millis()-screenStatusAt < 1000)) return;
  screenStatusAt = millis();
  bool wifi = WiFi.status() == WL_CONNECTED, ready = rfidOk.load();
  uint8_t auth = screenAuth.load(), language = playerLanguage.load();
  uint8_t state = auth | (wifi ? 8 : 0) | (ready ? 16 : 0);
  IPAddress ip = wifi ? WiFi.localIP() : IPAddress(0,0,0,0);
  if (!force && state == screenState && language == screenLanguage && uint32_t(ip) == screenIp) return;
  screenState = state; screenLanguage = language; screenIp = uint32_t(ip);
  String address = wifi ? "Web: http://" + ip.toString() : PlayerLanguage::get(language).noAddress;
  digitalWrite(SS_PIN, HIGH);
  PlayerScreen::status(tft,wifi,ready,auth,address.c_str(),language);
}
void updateWifiSetupScreen() {
  bool portal = WifiSetup::active();
  uint8_t language = playerLanguage.load(), state = uint8_t(WifiSetup::state());
  uint8_t flags = (rfidOk.load()?1:0) | (WiFi.status()==WL_CONNECTED?2:0) | ((screenAuth.load()&1)?4:0);
  if (portal && (!showingWifiSetup || screenPortalState != uint8_t(state|(flags<<3)) || screenLanguage != language)) {
    digitalWrite(SS_PIN,HIGH);
    String connectedUrl="http://"+WiFi.localIP().toString();
    PlayerScreen::wifiSetup(tft,language,WifiSetup::networkName(),WifiSetup::setupPassword(),state,flags,connectedUrl.c_str());
    screenPortalState=state|(flags<<3);screenLanguage=language;showingWifiSetup=true;infoVisible=false;
  } else if (!portal && showingWifiSetup) {
    showingWifiSetup=false;screenState=255;
    digitalWrite(SS_PIN,HIGH);PlayerScreen::illustration(tft);infoVisible=true;updateInfoScreen(true);
  }
}
void infoScreen() {
  digitalWrite(SS_PIN, HIGH);
  PlayerScreen::illustration(tft);
  infoVisible = true;
  updateInfoScreen(true);
}
bool decodeCover(uint8_t* bytes, size_t size, bool draw, bool flashImage=false) {
  uint32_t presentation=loading.generation;
  JpegDec.abort();
  int decoded=flashImage?JpegDec.decodeFsFile(SPIFFS.open(ArtworkSpool::Path,FILE_READ)):JpegDec.decodeArray(bytes,size);
  if (decoded<=0 || !JpegDec.width || !JpegDec.height || JpegDec.width > 640 || JpegDec.height > 640) { JpegDec.abort(); return false; }
  const CoverLayout::Layout layout(JpegDec.width,JpegDec.height);
  uint16_t row[320];
  int expected = JpegDec.MCUSPerRow * JpegDec.MCUSPerCol, count = 0;
  if (draw) { infoVisible = false; digitalWrite(SS_PIN, HIGH); tft.fillScreen(ILI9341_BLACK); }
  while (JpegDec.read()) {
    ++count;
    if (draw) {
      int sx=JpegDec.MCUx*JpegDec.MCUWidth,sy=JpegDec.MCUy*JpegDec.MCUHeight;
      int endX=min(sx+int(JpegDec.MCUWidth),int(JpegDec.width));
      int endY=min(sy+int(JpegDec.MCUHeight),int(JpegDec.height));
      int left=max(0,layout.left(sx)),right=min(320,layout.left(endX));
      int top=max(0,layout.top(sy)),bottom=min(240,layout.top(endY));
      // Scale each decoded block into a bounded scanline; no full-screen buffer.
      for(int y=top;y<bottom && right>left;++y) {
        int sourceRow=(layout.sourceY(y)-sy)*JpegDec.MCUWidth;
        for(int x=left;x<right;++x)row[x-left]=JpegDec.pImage[sourceRow+layout.sourceX(x)-sx];
        digitalWrite(SS_PIN,HIGH);tft.drawRGBBitmap(left,y,row,right-left,1);
      }
    }
    if (millis() - lastPoll >= 100) pollCard();
    if(presentation!=loading.generation){JpegDec.abort();return false;}
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
  bool coverVisible=false, debugVisible=false;
  uint32_t debugDrawAt=0, debugRevision=UINT32_MAX;
  DisplaySettings::LogSnapshot debugLog;
#else
  logMessage("[Display] TFT disabled; RFID and web controls remain enabled");
#endif
  readerHealth(true, "startup");
  uint32_t lastHealth = millis();
  for (;;) {
    if (millis() - lastPoll >= 100) pollCard();
    if (millis() - lastHealth >= (rfidOk ? 30000UL : 5000UL)) { readerHealth(false, "periodic register check"); lastHealth = millis(); }
    DisplayJob job{};
    if (xQueueReceive(displayJobs, &job, 0) == pdTRUE) {
#if PLAYER_HAS_DISPLAY
      if (WifiSetup::active() && job.action != DisplayAction::Reset) {
        if(job.flashImage)ArtworkSpool::release();
        free(job.image); // Setup instructions take priority over artwork/manual drawing.
        if(job.action==DisplayAction::Image)logMessage("[Art] Display deferred while Wi-Fi setup is open");
      } else
#endif
      switch (job.action) {
#if PLAYER_HAS_DISPLAY
        case DisplayAction::LoadingFailed: loading.fail(job.jobId,millis(),job.loadingError);break;
        case DisplayAction::Image:
          if(!loading.accepts(job.jobId)) {
            free(job.image);if(job.flashImage)ArtworkSpool::release();break;
          }
          if(job.flashImage) {
            if(decodeCover(nullptr,job.size,false,true)) {
              loading.stop();free(cachedImage);cachedImage=nullptr;cachedSize=0;
              if(!DisplaySettings::debug(displaySettings.load())) {
                loading.stop();coverVisible=decodeCover(nullptr,job.size,true,true);screenAt=millis();
                logMessage(coverVisible?"[Art] Full-quality cover displayed from flash":"[Art] Flash cover draw failed");
              }
            }else {logMessage("[Art] Flash JPEG decode failed; existing screen retained");loading.fail(job.jobId,millis(),4);}
            ArtworkSpool::release();break;
          }
          if (decodeCover(job.image, job.size, false)) {
            loading.stop();free(cachedImage); cachedImage = job.image; cachedSize = job.size;
            if (!DisplaySettings::debug(displaySettings.load())) {
              loading.stop();coverVisible=decodeCover(cachedImage, cachedSize, true); screenAt = millis();
              logMessage(coverVisible?"[Art] Cover displayed":"[Art] Cover draw failed");
            }
            // A large persistent cache would starve the NEXT Spotify TLS request.
            // The TFT retains its pixels; Last cover can fetch again when uncached.
            if(cachedSize>24*1024) {
              free(cachedImage);cachedImage=nullptr;cachedSize=0;
              logMessage("[Art] Large JPEG released after display to preserve HTTPS memory");
            }
          } else { free(job.image);loading.fail(job.jobId,millis(),4); logMessage("[Art] JPEG invalid; previous cover retained"); }
          break;
        case DisplayAction::Cached:
          loading.stop();
          if (!cachedImage) {
            logMessage("[Art] No cached cover; requesting artwork");
            Job request{};request.command=Command::Artwork;submit(request);
            if(!DisplaySettings::debug(displaySettings.load())){infoScreen();screenAt=millis();coverVisible=false;}
          } else if (!DisplaySettings::debug(displaySettings.load())) {
            coverVisible=decodeCover(cachedImage,cachedSize,true);screenAt=millis();
            logMessage(coverVisible?"[Art] Cached cover displayed":"[Art] Cached cover draw failed");
          }
          break;
        case DisplayAction::Info: loading.stop();if (!DisplaySettings::debug(displaySettings.load())) { infoScreen(); screenAt = millis(); coverVisible=false; } break;
        case DisplayAction::Clear: loading.stop();if (!DisplaySettings::debug(displaySettings.load())) { infoVisible = false; digitalWrite(SS_PIN, HIGH); tft.fillScreen(ILI9341_BLACK); screenAt = 0; coverVisible=false; } break;
#else
        case DisplayAction::Image: if(job.flashImage)ArtworkSpool::release();free(job.image); break;
        case DisplayAction::Cached: case DisplayAction::Info: case DisplayAction::Clear: case DisplayAction::LoadingFailed: break;
#endif
        case DisplayAction::Reset: readerHealth(true, "dashboard reset"); break;
      }
    }
#if PLAYER_HAS_DISPLAY
    xQueueReceive(tftLogQueue,&debugLog,0); // Keep only the latest bounded snapshot.
    bool wasSetup = showingWifiSetup;
    updateWifiSetupScreen();
    if (wasSetup && !showingWifiSetup) { screenAt=millis(); coverVisible=false; }
    uint32_t settings=displaySettings.load();
    if (WifiSetup::active()) {
      loading.drawn=false;debugVisible=false; coverVisible=false;
    } else if (DisplaySettings::debug(settings)) {
      loading.drawn=false;bool entering=!debugVisible;
      if (entering || (debugLog.revision!=debugRevision && millis()-debugDrawAt>=1000)) {
        digitalWrite(SS_PIN,HIGH);
        if (entering) {
          tft.fillScreen(ILI9341_BLACK); tft.setTextWrap(false); tft.setTextSize(1);
          tft.setTextColor(ILI9341_CYAN); tft.setCursor(4,8); tft.print("LIVE DEBUG LOG  |  auto refresh 1s");
        }
        tft.fillRect(0,28,320,212,ILI9341_BLACK); tft.setTextSize(1); tft.setTextColor(ILI9341_WHITE);
        for(size_t row=0;row<DisplaySettings::LogSnapshot::Rows;++row) {
          tft.setCursor(4,30+row*11); tft.print(debugLog.lines[row]);
          if(millis()-lastPoll>=100)pollCard();
        }
        debugDrawAt=millis();debugRevision=debugLog.revision;
      }
      debugVisible=true;infoVisible=false;coverVisible=false;
    } else {
      if(debugVisible) {
        debugVisible=false;
        if(cachedImage) {decodeCover(cachedImage,cachedSize,true);coverVisible=true;}
        else infoScreen();
        screenAt=millis();
      }
      if(loading.expired(millis())) {loading.stop();infoScreen();screenAt=millis();coverVisible=false;}
      if(loading.active) {
        infoVisible=false;coverVisible=false;digitalWrite(SS_PIN,HIGH);
        if(!loading.drawn){LoadingScreen::show(tft,playerLanguage.load(),loading.error);loading.drawn=true;}
        if(loading.frameDue(millis())){LoadingScreen::frame(tft,(millis()-loading.started)/120);loading.frameAt=millis();}
      }
      updateInfoScreen();
      // Cover timeout returns to the startup screen, then blanks after 30 minutes.
      uint32_t seconds=coverVisible?DisplaySettings::seconds(settings):DisplaySettings::StartupSeconds;
      if((coverVisible || infoVisible) && DisplaySettings::expired(millis(),screenAt,seconds)) {
        if(coverVisible) {
          infoScreen();coverVisible=false;screenAt=millis();
        } else {
          infoVisible=false;digitalWrite(SS_PIN,HIGH);tft.fillScreen(ILI9341_BLACK);screenAt=0;
        }
      }
    }
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
  doc["wifi_setup_active"] = WifiSetup::active();
  doc["wifi_setup_ssid"] = WifiSetup::networkName();
  doc["wifi_setup_password"] = WifiSetup::setupPassword();
  doc["display_debug"] = DisplaySettings::debug(displaySettings.load());
  doc["cover_seconds"] = DisplaySettings::seconds(displaySettings.load());
  doc["language"] = PlayerLanguage::code(playerLanguage.load());
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
  doc["wifi_connected"] = WiFi.status()==WL_CONNECTED; doc["wifi_ssid"] = WiFi.SSID();
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
void handleDisplaySettings() {
  if(!authorized(true)) return;
#if !PLAYER_HAS_DISPLAY
  webServer.send(409,"application/json","{\"message\":\"This player has no display\"}"); return;
#endif
  String body=webServer.arg("plain");JsonDocument doc;
  if(body.length()>128 || deserializeJson(doc,body) || !doc["debug"].is<bool>() ||
     !doc["cover_seconds"].is<uint32_t>() || !DisplaySettings::validSeconds(doc["cover_seconds"].as<uint32_t>())) {
    webServer.send(400,"application/json","{\"message\":\"Use a debug boolean and cover duration 10-86400 seconds, or 0 for always on\"}");return;
  }
  uint32_t settings=DisplaySettings::pack(doc["debug"].as<bool>(),doc["cover_seconds"].as<uint32_t>());
  // Commit both settings in a single NVS value before applying them to the TFT.
  if(!uiStorageReady || (uiPreferences.getUInt("display",UINT32_MAX)!=settings && uiPreferences.putUInt("display",settings)!=sizeof(uint32_t))) {
    webServer.send(507,"application/json","{\"message\":\"Display settings could not be saved; previous settings retained\"}");return;
  }
  displaySettings.store(settings);
  webServer.send(200,"application/json","{\"message\":\"Display settings saved\"}");
}
void handleLanguage() {
  if (!authorized(true)) return;
  String body=webServer.arg("plain"); JsonDocument doc;
  if (body.length()>128 || deserializeJson(doc,body) || !doc["language"].is<const char*>()) {
    webServer.send(400,"application/json","{\"message\":\"Invalid language request\"}"); return;
  }
  int language=PlayerLanguage::parse(doc["language"].as<const char*>());
  if (language<0) { webServer.send(400,"application/json","{\"message\":\"Supported languages: en, de, fr, es\"}"); return; }
  if (!uiStorageReady || uiPreferences.getUChar("language",255) != language) {
    if (!uiStorageReady || uiPreferences.putUChar("language",uint8_t(language))!=1) {
      webServer.send(507,"application/json","{\"message\":\"Language could not be saved; previous setting retained\"}"); return;
    }
    playerLanguage.store(uint8_t(language));
  }
  webServer.send(200,"application/json","{\"message\":\"Screen language saved\",\"language\":\"" + String(PlayerLanguage::code(language)) + "\"}");
}
void setupWeb() {
  const char* headers[] = {"X-Requested-With"}; webServer.collectHeaders(headers, 1);
  webServer.on("/", HTTP_GET, [] { if (WifiSetup::servePortal(webServer)) return; if (authorized()) webServer.send_P(200, "text/html; charset=utf-8", dashboard); });
  webServer.on("/api/status", HTTP_GET, handleStatus);
  webServer.on("/api/language", HTTP_POST, handleLanguage);
  webServer.on("/api/display_settings", HTTP_POST, handleDisplaySettings);
  WifiSetup::routes(webServer);
  webServer.on("/api/wifi_setup", HTTP_POST, [] {
    if(!authorized(true)) return;
    WifiSetup::requestStart();
    webServer.send(202,"application/json","{\"message\":\"Setup network starting; follow the instructions below or on screen\"}");
  });
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
  webServer.on("/api/reload_cover", HTTP_POST, [] {
#if PLAYER_HAS_DISPLAY
    handleCommand(Command::Artwork);
#else
    if(authorized(true))webServer.send(409,"application/json","{\"message\":\"This player has no display\"}");
#endif
  });
  webServer.on("/api/clear_screen", HTTP_POST, [] { handleDisplay(DisplayAction::Clear); });
  webServer.on("/api/restart", HTTP_POST, [] {
    if (!authorized(true)) return;
    webServer.send(200, "application/json", "{\"message\":\"Restarting\"}"); delay(100); ESP.restart();
  });
  webServer.onNotFound([] { if(WifiSetup::servePortal(webServer)) return; webServer.send(404, "application/json", "{\"message\":\"Not found\"}"); });
  webServer.begin();
}
void setup() {
  Serial.begin(115200);
  logs = xQueueCreate(24, sizeof(LogLine)); jobs = xQueueCreate(4, sizeof(Job));
  results = xQueueCreate(8, sizeof(Result)); displayJobs = xQueueCreate(3, sizeof(DisplayJob));
#if PLAYER_HAS_DISPLAY
  tftLogQueue=xQueueCreate(1,sizeof(DisplaySettings::LogSnapshot));
  if(!tftLogQueue) {Serial.println("Fatal: cannot allocate TFT log queue");while(true)delay(1000);}
#endif
  if (!logs || !jobs || !results || !displayJobs || !preferences.begin("spotify", false)) {
    Serial.println("Fatal: cannot allocate queues/open NVS"); while (true) delay(1000);
  }
  uiStorageReady = uiPreferences.begin("player_ui",false);
  uint32_t savedDisplay=uiStorageReady?uiPreferences.getUInt("display",DisplaySettings::DefaultSeconds):DisplaySettings::DefaultSeconds;
  displaySettings.store(DisplaySettings::validSeconds(DisplaySettings::seconds(savedDisplay))?savedDisplay:DisplaySettings::DefaultSeconds);
  uint8_t savedLanguage = uiStorageReady ? uiPreferences.getUChar("language",PlayerLanguage::Default) : PlayerLanguage::Default;
  playerLanguage.store(savedLanguage<PlayerLanguage::Count?savedLanguage:PlayerLanguage::Default);
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
#if PLAYER_HAS_DISPLAY
  screenAuth.store(saved.isEmpty() ? 2 : 0);
#endif
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
  WifiSetup::begin(ssid, pass);
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
  WifiSetup::tick();
  static bool wasConnected = false;
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wasConnected) {
    MDNS.end(); if (MDNS.begin(DeviceAuth::hostname())) MDNS.addService("http", "tcp", 80);
    logMessage("[WiFi] Connected at " + WiFi.localIP().toString());
  }
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
    if(result.command==Command::Play && result.code!=202 && !ok(result.code)) {
      DisplayJob failure{};failure.action=DisplayAction::LoadingFailed;failure.jobId=result.id;
      xQueueSend(displayJobs,&failure,0);
    }
    if (result.command == Command::Devices) devicesJson = result.code == 200 && result.payload ? result.payload : "{\"devices\":[]}";
    free(result.payload);
    if (result.image || result.flashImage) {
      DisplayJob display{}; display.action = DisplayAction::Image; display.image = result.image; display.size = result.imageSize;display.flashImage=result.flashImage;display.jobId=result.id;
      if (xQueueSend(displayJobs, &display, 0) != pdTRUE) {if(result.flashImage)ArtworkSpool::release();free(result.image);logMessage("[Art] Display queue full; use Reload cover");}
    }
  }
  LogLine line{};
#if PLAYER_HAS_DISPLAY
  static DisplaySettings::LogSnapshot tftLogs;
  bool logsChanged=false;
#endif
  for (int i = 0; i < 8 && xQueueReceive(logs, &line, 0) == pdTRUE; ++i) {
    logHistory[logHead] = line.text; logHead = (logHead + 1) % 48; if (logCount < 48) ++logCount;
    Serial.println(line.text);
#if PLAYER_HAS_DISPLAY
    tftLogs.append(line.text);logsChanged=true;
#endif
  }
#if PLAYER_HAS_DISPLAY
  if(logsChanged)xQueueOverwrite(tftLogQueue,&tftLogs);
#endif
  delay(5);
}
