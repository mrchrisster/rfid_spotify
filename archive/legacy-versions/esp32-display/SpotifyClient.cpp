#include "SpotifyClient.h"
#include <HTTPClient.h>
#include <base64.h>
#include <ArduinoJson.h>

// Define LOG macro if not already defined
#ifndef LOG
// If telnetClient is available from your main code, you could forward output there.
// For now, we print to Serial.
#define LOG(msg) do { Serial.println(msg); } while(0)
#endif

// ——————— DigiCert Global Root CA ———————
const char* digicert_root_ca = R"EOF(
-----BEGIN CERTIFICATE-----
MIIEyDCCA7CgAwIBAgIQDPW9BitWAvR6uFAsI8zwZjANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0yMTAzMzAwMDAwMDBaFw0zMTAzMjkyMzU5NTlaMFkxCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxMzAxBgNVBAMTKkRpZ2lDZXJ0IEdsb2Jh
bCBHMiBUTFMgUlNBIFNIQTI1NiAyMDIwIENBMTCCASIwDQYJKoZIhvcNAQEBBQAD
ggEPADCCAQoCggEBAMz3EGJPprtjb+2QUlbFbSd7ehJWivH0+dbn4Y+9lavyYEEV
cNsSAPonCrVXOFt9slGTcZUOakGUWzUb+nv6u8W+JDD+Vu/E832X4xT1FE3LpxDy
FuqrIvAxIhFhaZAmunjZlx/jfWardUSVc8is/+9dCopZQ+GssjoP80j812s3wWPc
3kbW20X+fSP9kOhRBx5Ro1/tSUZUfyyIxfQTnJcVPAPooTncaQwywa8WV0yUR0J8
osicfebUTVSvQpmowQTCd5zWSOTOEeAqgJnwQ3DPP3Zr0UxJqyRewg2C/Uaoq2yT
zGJSQnWS+Jr6Xl6ysGHlHx+5fwmY6D36g39HaaECAwEAAaOCAYIwggF+MBIGA1Ud
EwEB/wQIMAYBAf8CAQAwHQYDVR0OBBYEFHSFgMBmx9833s+9KTeqAx2+7c0XMB8G
A1UdIwQYMBaAFE4iVCAYlebjbuYP+vq5Eu0GF485MA4GA1UdDwEB/wQEAwIBhjAd
BgNVHSUEFjAUBggrBgEFBQcDAQYIKwYBBQUHAwIwdgYIKwYBBQUHAQEEajBoMCQG
CCsGAQUFBzABhhhodHRwOi8vb2NzcC5kaWdpY2VydC5jb20wQAYIKwYBBQUHMAKG
NGh0dHA6Ly9jYWNlcnRzLmRpZ2ljZXJ0LmNvbS9EaWdpQ2VydEdsb2JhbFJvb3RH
Mi5jcnQwQgYDVR0fBDswOTA3oDWgM4YxaHR0cDovL2NybDMuZGlnaWNlcnQuY29t
L0RpZ2lDZXJ0R2xvYmFsUm9vdEcyLmNybDA9BgNVHSAENjA0MAsGCWCGSAGG/WwC
ATAHBgVngQwBATAIBgZngQwBAgEwCAYGZ4EMAQICMAgGBmeBDAECAzANBgkqhkiG
9w0BAQsFAAOCAQEAkPFwyyiXaZd8dP3A+iZ7U6utzWX9upwGnIrXWkOH7U1MVl+t
wcW1BSAuWdH/SvWgKtiwla3JLko716f2b4gp/DA/JIS7w7d7kwcsr4drdjPtAFVS
slme5LnQ89/nD/7d+MS5EHKBCQRfz5eeLjJ1js+aWNJXMX43AYGyZm0pGrFmCW3R
bpD0ufovARTFXFZkAdl9h6g4U5+LXUZtXMYnhIHUfoyMo5tS58aI7Dd8KvvwVVo4
chDYABPPTHPbqjc1qCmBaZx2vN4Ye5DUys/vZwP9BFohFrH/6j/f3IL16/RZkiMN
JCqVJUzKoZHm1Lesh3Sz8W2jmdv51b2EQJ8HmA==
-----END CERTIFICATE-----
)EOF";


SpotifyClient::SpotifyClient(String clientId, String clientSecret, String deviceName, String refreshToken) {
    this->clientId = clientId;
    this->clientSecret = clientSecret;
    this->deviceName = deviceName;
    this->refreshToken = refreshToken;

    client.setCACert(digicert_root_ca);
    tokenValid = false;
    deviceId = "";
    lastTokenRefresh = 0;
    // tokenRefreshInterval is initialized to a default of 3600000 (1 hour) in the header
}

void SpotifyClient::FetchToken() {
    tokenValid = false; // Mark token invalid before fetching
    LOG("[SpotifyClient] Fetching new token...");

    String body = "grant_type=refresh_token&refresh_token=" + refreshToken;
    String authorizationRaw = clientId + ":" + clientSecret;
    String authorization = base64::encode(authorizationRaw);

    WiFiClientSecure httpsTokenClient;
    httpsTokenClient.setCACert(digicert_root_ca);

    HTTPClient http;
    http.begin(httpsTokenClient, "https://accounts.spotify.com/api/token");

    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("Authorization", "Basic " + authorization);

    const int maxAttempts = 3;
    bool success = false;

    for (int attempts = 0; attempts < maxAttempts; attempts++) {
        int httpCode = http.POST(body);

        if (httpCode > 0) {
            String returnedPayload = http.getString();
            LOG("[SpotifyClient] Token fetch response: " + returnedPayload);

            if (httpCode == 200) {
                accessToken = ParseJson("access_token", returnedPayload);

                String expiresInStr = ParseJson("expires_in", returnedPayload);
                if (!expiresInStr.isEmpty()) {
                    int expiresIn = expiresInStr.toInt(); // Expiration in seconds
                    // Refresh 5 minutes early
                    tokenRefreshInterval = (expiresIn - 300) * 1000UL; 
                    lastTokenRefresh = millis();
                    tokenValid = true;
                    LOG("[SpotifyClient] Token refreshed successfully. Valid for " + String(expiresIn) + " seconds.");
                } else {
                    LOG("[SpotifyClient] Failed to parse expires_in. Using default interval.");
                    tokenValid = true; // Mark as valid but fallback to default interval
                    lastTokenRefresh = millis();
                }

                success = true;
                break;
            } else {
                LOG("[SpotifyClient] Failed to fetch token. HTTP Code: " + String(httpCode));
                LOG("[SpotifyClient] Response: " + returnedPayload);
            }
        } else {
            LOG("[SpotifyClient] Connection error: " + String(http.errorToString(httpCode)));
        }

        LOG("[SpotifyClient] Retrying in 2 seconds...");
        delay(2000);
    }

    http.end();

    if (!success) {
        LOG("[SpotifyClient] All attempts to refresh token failed. Token remains invalid.");
        tokenValid = false;
    }
}

bool SpotifyClient::IsTokenExpired() {
    // If current time exceeds lastTokenRefresh + tokenRefreshInterval, token is expired
    return millis() > (lastTokenRefresh + tokenRefreshInterval);
}

bool SpotifyClient::EnsureTokenFresh() {
    // If token is invalid or expired, attempt to refresh
    if (!tokenValid || IsTokenExpired()) {
        LOG("[SpotifyClient] Token is invalid or expired. Attempting refresh...");
        FetchToken();
        if (!tokenValid || IsTokenExpired()) {
            LOG("[SpotifyClient] Unable to obtain a fresh token.");
            return false;
        }
    }
    return true;
}

int SpotifyClient::Play(String context_uri) {
    LOG("[SpotifyClient] Play()");

    if (!EnsureTokenFresh()) {
        LOG("[SpotifyClient] Cannot play without a valid token.");
        return 401; // Unauthorized
    }

    if (deviceId.isEmpty()) {
        LOG("[SpotifyClient] Device ID is empty. Attempting to refresh devices...");
        GetDevices();
        if (deviceId.isEmpty()) {
            LOG("[SpotifyClient] Error: Unable to set deviceId. Aborting playback.");
            return 404;
        }
    }

    String body = "{\"context_uri\":\"" + context_uri + "\",\"offset\":{\"position\":0,\"position_ms\":0}}";
    String url = "https://api.spotify.com/v1/me/player/play?device_id=" + deviceId;
    HttpResult result = CallAPI("PUT", url, body);

    if (result.httpCode != 200 && result.httpCode != 204) {
        LOG("[SpotifyClient] Error: Unexpected HTTP Code: " + String(result.httpCode));
    }
    return result.httpCode;
}

int SpotifyClient::Shuffle() {
    LOG("[SpotifyClient] Shuffle()");
    if (!EnsureTokenFresh()) {
        LOG("[SpotifyClient] Cannot shuffle without a valid token.");
        return 401; 
    }
    HttpResult result = CallAPI("PUT", "https://api.spotify.com/v1/me/player/shuffle?state=true&device_id=" + deviceId, "{}");
    return result.httpCode;
}

int SpotifyClient::Next() {
    LOG("[SpotifyClient] Next()");
    if (!EnsureTokenFresh()) {
        LOG("[SpotifyClient] Cannot skip track without a valid token.");
        return 401; 
    }
    HttpResult result = CallAPI("POST", "https://api.spotify.com/v1/me/player/next?device_id=" + deviceId, "{}");
    return result.httpCode;
}

String SpotifyClient::GetDevices() {
    if (!EnsureTokenFresh()) {
        LOG("[SpotifyClient] Cannot fetch devices without a valid token.");
        return "";
    }

    const int maxRetries = 3;
    const int retryDelay = 2000;
    String foundDeviceId = "";

    for (int attempt = 1; attempt <= maxRetries; attempt++) {
        char buffer[100];
        sprintf(buffer, "[SpotifyClient] Fetching devices (Attempt %d)...", attempt);
        LOG(buffer);

        HttpResult result = CallAPI("GET", "https://api.spotify.com/v1/me/player/devices", "");
        if (result.httpCode == 200) {
            LOG("[SpotifyClient] Devices response: " + result.payload);
            foundDeviceId = GetDeviceId(result.payload);
            if (!foundDeviceId.isEmpty()) {
                deviceId = foundDeviceId;
                LOG("[SpotifyClient] Found device ID: " + foundDeviceId);
                return foundDeviceId;
            } else {
                LOG("[SpotifyClient] Device not found. Retrying...");
            }
        } else {
            LOG("[SpotifyClient] Failed to fetch devices. HTTP Code: " + String(result.httpCode));
            LOG("Response: " + result.payload);
        }

        delay(retryDelay);
    }

    LOG("[SpotifyClient] Max retries reached. Device not found.");
    return foundDeviceId;
}

HttpResult SpotifyClient::CallAPI(String method, String url, String body) {
    HttpResult result;
    result.httpCode = 0;
    result.payload = "";

    if (!EnsureTokenFresh()) {
        LOG("[SpotifyClient] Cannot call API without a valid token.");
        return result;
    }

    for (int attempts = 0; attempts < 2; attempts++) {
        WiFiClientSecure httpsClient;
        httpsClient.setCACert(digicert_root_ca);

        HTTPClient http;
        http.begin(httpsClient, url);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", "Bearer " + accessToken);

        if (body.isEmpty() && (method == "PUT" || method == "POST")) {
            body = "{}";
            http.addHeader("Content-Length", String(body.length()));
        }

        if      (method == "PUT")    result.httpCode = http.PUT(body);
        else if (method == "POST")   result.httpCode = http.POST(body);
        else if (method == "GET")    result.httpCode = http.GET();
        else {
            LOG("[SpotifyClient] Unsupported HTTP method.");
            http.end();
            break;
        }

        // if we got a 401 on the first attempt, refresh & retry
        if (result.httpCode == 401 && attempts == 0) {
            LOG("[SpotifyClient] Access token expired mid-call. Refreshing...");
            tokenValid = false;
            http.end();
            if (!EnsureTokenFresh()) {
                LOG("[SpotifyClient] Unable to refresh token after 401. Aborting call.");
                return result;
            }
            continue;
        }

        if (result.httpCode > 0) {
            // only read a payload if it's not 204 No Content
            if (result.httpCode != 204) {
                result.payload = http.getString();
            }
            http.end();
            break;
        } else {
            LOG("[SpotifyClient] Failed to connect to URL: " + url);
            http.end();
        }
    }

    return result;
}


void SpotifyClient::ResetState() {
    LOG("[SpotifyClient] Resetting Spotify client state...");
    EnsureTokenFresh();
    GetDevices();
}

String SpotifyClient::GetDeviceId(String json) {
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        LOG("[SpotifyClient] JSON parsing failed: " + String(error.c_str()));
        return "";
    }

    JsonArray devices = doc["devices"].as<JsonArray>();
    for (JsonObject device : devices) {
        String name = device["name"].as<String>();
        String id = device["id"].as<String>();

        LOG("[SpotifyClient] Device name: " + name + ", ID: " + id);

        if (name == deviceName) {
            return id;
        }
    }

    LOG("[SpotifyClient] " + deviceName + " device name not found.");
    return "";
}

String SpotifyClient::ParseJson(String key, String json) {
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        LOG("[SpotifyClient] JSON parsing failed: " + String(error.c_str()));
        return "";
    }

    return doc[key] | "";
}
