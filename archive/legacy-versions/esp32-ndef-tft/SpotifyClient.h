#pragma once
#include <Arduino.h>
#include "SafeTlsClient.h"
#include <HTTPClient.h>

void logMessage(const String& message);
struct HttpResult { int httpCode = 0; String payload; };
class SpotifyClient {
public:
  using TokenSaver = bool (*)(const String&, bool pkce);
  SpotifyClient(String id, String secret, String speaker, String token);
  void SetRefreshTokenCallback(TokenSaver saver) { saveToken = saver; }
  void SetRefreshToken(const String& token, bool pkce = false);
  bool AuthorizeCode(const String& code, const String& verifier, const String& redirectUri);
  bool ReplaceRefreshToken(const String& candidate);
  bool EnsureTokenFresh(bool force = false);
  bool IsTokenValid() const;
  bool HasUnsavedToken() const { return persistencePending; }
  bool IsRevoked() const { return revoked; }
  uint32_t RetryInMs() const;
  int LastError() const { return lastError; }
  const String& DeviceId() const { return deviceId; }
  const String& DeviceName() const { return deviceName; }
  void SelectDevice(const String& name, const String& id = "");
  void ResetState() { deviceId = ""; }
  String GetDevices();
  int Play(const String& uri);
  int Next();
  HttpResult CallAPI(const String& method, const String& url, const String& body = "");
  // ESP32: positive count with null buffer transfers ownership of ArtworkSpool file.
  int DownloadArtwork(const String& url, uint8_t*& buffer, size_t maxCapacity);
  int DownloadFile(const String& url, uint8_t* buffer, size_t capacity);
private:
  int download(const String& url, uint8_t*& buffer, size_t capacity, bool allocate);
  SafeTlsClient client;
  String clientId, clientSecret, deviceName, deviceId, refreshToken, accessToken;
  bool pkce = false;
  bool tokenValid = false, revoked = false, persistencePending = false;
  uint32_t refreshedAt = 0, validFor = 0, cooldownAt = 0, cooldownFor = 0;
  int lastError = 0;
  TokenSaver saveToken = nullptr;
  bool exchange(const String& candidate, bool replacement);
  bool acceptTokens(const String& payload, const String& fallback, bool replacement, bool usePkce);
  bool prepare(HTTPClient& http, const String& url);
  void cooldown(int code, const String& retryAfter);
  bool resolveDevice();
};
