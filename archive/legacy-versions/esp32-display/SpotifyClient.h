#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>

extern const char* digicert_root_ca;   // ← declare the CA here

struct HttpResult {
    int httpCode;
    String payload;
};

class SpotifyClient {
public:
    SpotifyClient(String clientId, String clientSecret, String deviceName, String refreshToken);

    void FetchToken();
    int Play(String context_uri);
    int Shuffle();
    int Next();
    String GetDevices();
    HttpResult CallAPI(String method, String url, String body);
    void ResetState();
    bool IsTokenValid()    { return tokenValid; }
    bool IsTokenExpired();
    bool EnsureTokenFresh();
    unsigned long GetTokenRefreshInterval() const { return tokenRefreshInterval; }

private:
    // remove digicert_root_ca from here!
    WiFiClientSecure client;
    String clientId;
    String clientSecret;
    String accessToken;
    String refreshToken;
    String deviceId;
    String deviceName;

    String ParseJson(String key, String json);
    String GetDeviceId(String json);
    String ParseDeviceId(String json);
    int MakeAPIRequest(String method, String url, String body);

    bool tokenValid = false;
    unsigned long tokenRefreshInterval = 3600000;
    unsigned long lastTokenRefresh      = 0;
    unsigned long tokenExpiresIn        = 0;
};
