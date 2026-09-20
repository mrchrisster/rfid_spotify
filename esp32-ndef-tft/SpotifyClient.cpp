#include "SpotifyClient.h"
#include "SafeNdef.h"
#include "Reliability.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <base64.h>
#include <time.h>
#include <new>

// Mozilla trust bundle embedded by the pinned ESP32 core; never disable verification.
extern const uint8_t bundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t bundleEnd[] asm("_binary_x509_crt_bundle_end");
namespace {
constexpr size_t MaxJson = 24 * 1024;
class BoundedSink : public Stream {
  uint8_t* buffer;
  size_t capacity;
  uint32_t started = millis();
public:
  size_t count = 0;
  bool failed = false;
  BoundedSink(uint8_t* buffer, size_t capacity) : buffer(buffer), capacity(capacity) {}
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* data, size_t size) override {
    if (failed || size > capacity - count || millis() - started > 10000) { failed = true; return 0; }
    memcpy(buffer + count, data, size); count += size; return size;
  }
};
bool readBody(HTTPClient& http, String& body) {
  if (http.getSize() > int(MaxJson)) return false;
  size_t capacity = http.getSize() >= 0 ? size_t(http.getSize()) : MaxJson;
  auto* bytes = static_cast<uint8_t*>(malloc(capacity + 1));
  if (!bytes) return false;
  BoundedSink sink(bytes, capacity);
  int read = http.writeToStream(&sink);
  bool ok = read >= 0 && !sink.failed && (http.getSize() < 0 || sink.count == size_t(http.getSize()));
  if (ok) { bytes[sink.count] = 0; body = reinterpret_cast<char*>(bytes); ok = body.length() == sink.count; }
  free(bytes); return ok;
}
String encoded(const String& value) {
  static const char hex[] = "0123456789ABCDEF";
  String result;
  for (size_t i = 0; i < value.length(); ++i) {
    uint8_t c = value[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') result += char(c);
    else { result += '%'; result += hex[c >> 4]; result += hex[c & 15]; }
  }
  return result;
}
bool success(int code) { return code == 200 || code == 204; }
}
SpotifyClient::SpotifyClient(String id, String secret, String speaker, String token)
 : clientId(id), clientSecret(secret), deviceName(speaker), refreshToken(token) {}
void SpotifyClient::SetRefreshToken(const String& token, bool usePkce) {
  pkce = usePkce; persistencePending = false;
  refreshToken = token; accessToken = ""; tokenValid = false; revoked = false;
}
void SpotifyClient::SelectDevice(const String& name, const String& id) { deviceName = name; deviceId = id; }
bool SpotifyClient::IsTokenValid() const { return tokenValid && uint32_t(millis() - refreshedAt) < validFor; }
uint32_t SpotifyClient::RetryInMs() const {
  return remainingDelay(millis(), cooldownAt, cooldownFor);
}
void SpotifyClient::cooldown(int code, const String& retryAfter) {
  uint32_t wait = 0;
  if (code == 429) {
    unsigned long seconds = strtoul(retryAfter.c_str(), nullptr, 10);
    if (!seconds) seconds = 30;
    wait = (seconds > 86400 ? 86400 : seconds) * 1000UL;
  } else if (code < 0 || code >= 500) wait = 5000;
  if (wait > RetryInMs()) { cooldownAt = millis(); cooldownFor = wait; }
}
bool SpotifyClient::prepare(HTTPClient& http, const String& url) {
  if (RetryInMs()) { lastError = 429; return false; }
  if (WiFi.status() != WL_CONNECTED) { lastError = -1; return false; }
  if (time(nullptr) < 1700000000) { lastError = -2; return false; }
  if (!url.startsWith("https://")) { lastError = 400; return false; }
  client.stop();
  client.setCACertBundle(bundleStart, bundleEnd - bundleStart);
  client.setHandshakeTimeout(8);
  http.setConnectTimeout(3000); http.setTimeout(3000); http.setReuse(false);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) { lastError = -3; return false; }
  const char* headers[] = {"Retry-After"}; http.collectHeaders(headers, 1);
  return true;
}
bool SpotifyClient::exchange(const String& candidate, bool replacement) {
  if (candidate.isEmpty() || candidate.length() > 1024) { lastError = 400; return false; }
  HTTPClient http;
  if (!prepare(http, "https://accounts.spotify.com/api/token")) return false;
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  const bool usePkce = !replacement && pkce;
  if (!usePkce) http.addHeader("Authorization", "Basic " + base64::encode(clientId + ":" + clientSecret));
  String body = "grant_type=refresh_token&refresh_token=" + encoded(candidate);
  if (usePkce) body += "&client_id=" + encoded(clientId);
  int code = http.POST(body);
  lastError = code; cooldown(code, http.header("Retry-After"));
  String payload;
  bool bodyOk = code > 0 && readBody(http, payload);
  http.end(); client.stop();
  JsonDocument doc;
  if (!bodyOk || deserializeJson(doc, payload)) { if (code == 200) lastError = 502; return false; }
  if (code != 200) {
    if (!replacement && (doc["error"] == "invalid_grant" || doc["error"] == "invalid_client")) {
      revoked = true; tokenValid = false;
    }
    logMessage("[Auth] Token request failed (HTTP " + String(code) + ")"); return false;
  }
  return acceptTokens(payload, candidate, replacement, usePkce);
}
bool SpotifyClient::acceptTokens(const String& payload, const String& fallback, bool replacement, bool usePkce) {
  JsonDocument doc;
  if (deserializeJson(doc, payload)) { lastError = 502; return false; }
  String access = doc["access_token"] | "";
  uint32_t seconds = doc["expires_in"] | 0UL;
  String rotated = doc["refresh_token"] | fallback;
  if (access.isEmpty() || access.length() > 4096 || !seconds || seconds > 86400 || rotated.isEmpty() || rotated.length() > 1024) {
    lastError = 502; return false;
  }
  bool saved = true;
  if (replacement || rotated != refreshToken) saved = saveToken && saveToken(rotated, usePkce);
  // A rotated token may already have invalidated its predecessor; retain it in RAM even if NVS failed.
  pkce = usePkce; refreshToken = rotated; accessToken = access; tokenValid = true; revoked = false;
  validFor = (seconds > 300 ? seconds - 300 : seconds) * 1000UL; refreshedAt = millis();
  persistencePending = !saved;
  if (!saved) { lastError = 507; logMessage("[Auth] Token valid in RAM but persistence FAILED; do not reboot before retrying save"); return false; }
  logMessage("[Auth] Token refreshed"); return true;
}
bool SpotifyClient::AuthorizeCode(const String& code, const String& verifier, const String& redirectUri) {
  if (code.isEmpty() || code.length() > 1024 || verifier.length() != 43 || !redirectUri.startsWith("https://")) {
    lastError = 400; return false;
  }
  HTTPClient http;
  if (!prepare(http, "https://accounts.spotify.com/api/token")) return false;
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int status = http.POST("grant_type=authorization_code&client_id=" + encoded(clientId) + "&code=" + encoded(code) +
    "&redirect_uri=" + encoded(redirectUri) + "&code_verifier=" + encoded(verifier));
  lastError = status; cooldown(status, http.header("Retry-After"));
  String payload;
  bool bodyOk = status > 0 && readBody(http, payload);
  http.end(); client.stop();
  if (status != 200) return false; // A failed login never invalidates the existing credentials.
  if (!bodyOk) { lastError = 502; return false; }
  JsonDocument doc;
  if (deserializeJson(doc, payload) || doc["token_type"] != "Bearer") { lastError = 502; return false; }
  // If Spotify reports granted scopes, reject an account without playback permissions.
  if (doc["scope"].is<const char*>()) {
    String scopes = " " + doc["scope"].as<String>() + " ";
    if (scopes.indexOf(" user-read-playback-state ") < 0 || scopes.indexOf(" user-modify-playback-state ") < 0) {
      lastError = 403; return false;
    }
  }
  bool accepted = acceptTokens(payload, "", true, true);
  if (accepted || lastError == 507) ResetState();
  return accepted;
}
bool SpotifyClient::ReplaceRefreshToken(const String& candidate) { return exchange(candidate, true); }
bool SpotifyClient::EnsureTokenFresh(bool force) {
  if (persistencePending) {
    if (!saveToken || !saveToken(refreshToken, pkce)) { lastError = 507; return false; }
    persistencePending = false;
  }
  if (!force && IsTokenValid()) return true;
  if (revoked && !force) { lastError = 401; return false; }
  return exchange(refreshToken, false);
}
HttpResult SpotifyClient::CallAPI(const String& method, const String& url, const String& body) {
  HttpResult result;
  if (!url.startsWith("https://api.spotify.com/v1/")) { result.httpCode = 400; return result; }
  if (!EnsureTokenFresh()) { result.httpCode = lastError; return result; }
  for (int attempt = 0; attempt < 2; ++attempt) {
    HTTPClient http;
    if (!prepare(http, url)) { result.httpCode = lastError; return result; }
    http.addHeader("Authorization", "Bearer " + accessToken);
    http.addHeader("Content-Type", "application/json");
    // Arduino HTTPClient only supplies this header for nonempty bodies. Spotify
    // can reject empty playback PUT/POST requests with 411 when it is absent.
    if ((method == "PUT" || method == "POST") && body.isEmpty()) http.addHeader("Content-Length", "0");
    if (method == "GET") result.httpCode = http.GET();
    else if (method == "PUT") result.httpCode = http.PUT(body);
    else if (method == "POST") result.httpCode = http.POST(body);
    else result.httpCode = 400;
    cooldown(result.httpCode, http.header("Retry-After"));
    bool bodyOk = result.httpCode <= 0 || result.httpCode == 204 || readBody(http, result.payload);
    http.end(); client.stop();
    if (result.httpCode == 401) {
      tokenValid = false;
      if (attempt == 0 && EnsureTokenFresh()) continue;
    }
    if (result.httpCode == 404 && url.indexOf("/me/player") >= 0) ResetState();
    if (!bodyOk && success(result.httpCode)) result.httpCode = 502;
    lastError = result.httpCode;
    return result;
  }
  return result;
}
String SpotifyClient::GetDevices() {
  ResetState();
  HttpResult result = CallAPI("GET", "https://api.spotify.com/v1/me/player/devices");
  JsonDocument doc;
  if (result.httpCode != 200 || deserializeJson(doc, result.payload)) return "";
  for (JsonObject device : doc["devices"].as<JsonArray>()) {
    if (device["name"] == deviceName && !device["is_restricted"].as<bool>() && device["id"].is<const char*>()) {
      deviceId = device["id"].as<String>(); break;
    }
  }
  return deviceId;
}
bool SpotifyClient::resolveDevice() { return !deviceId.isEmpty() || !GetDevices().isEmpty(); }
int SpotifyClient::Play(const String& uri) {
  char normalized[SafeNdef::MaxUri];
  if (!SafeNdef::normalize(uri.c_str(), normalized, sizeof(normalized))) return 400;
  if (!resolveDevice()) return lastError == 200 ? 404 : lastError;
  JsonDocument doc;
  if (uri.startsWith("spotify:track:")) doc["uris"].to<JsonArray>().add(uri);
  else { doc["context_uri"] = uri; if (!uri.startsWith("spotify:artist:")) doc["offset"]["position"] = 0; }
  doc["position_ms"] = 0;
  String body; serializeJson(doc, body);
  return CallAPI("PUT", "https://api.spotify.com/v1/me/player/play?device_id=" + deviceId, body).httpCode;
}
int SpotifyClient::Next() {
  if (!resolveDevice()) return lastError == 200 ? 404 : lastError;
  return CallAPI("POST", "https://api.spotify.com/v1/me/player/next?device_id=" + deviceId).httpCode;
}
int SpotifyClient::DownloadFile(const String& url, uint8_t* buffer, size_t capacity) {
  // Only accept the CDN hostname supplied by Spotify; credentials are never attached here.
  if (!url.startsWith("https://i.scdn.co/")) return 0;
  HTTPClient http;
  if (!prepare(http, url)) return 0;
  int code = http.GET();
  if (code != 200 || http.getSize() > int(capacity)) { http.end(); client.stop(); return 0; }
  BoundedSink sink(buffer, capacity);
  int read = http.writeToStream(&sink);
  bool ok = read >= 0 && !sink.failed && sink.count > 0 && (http.getSize() < 0 || sink.count == size_t(http.getSize()));
  http.end(); client.stop();
  return ok ? sink.count : 0;
}
