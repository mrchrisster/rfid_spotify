#include <WiFi.h>
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

    HTTPClient http;
    String body = "grant_type=refresh_token&refresh_token=" + refreshToken;
    String authorizationRaw = clientId + ":" + clientSecret;
    String authorization = base64::encode(authorizationRaw);

    http.begin(client, "https://accounts.spotify.com/api/token");
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

    // Ensure we have a fresh token before making the call
    if (!EnsureTokenFresh()) {
        LOG("[SpotifyClient] Cannot call API without a valid token.");
        return result;
    }

    for (int attempts = 0; attempts < 2; attempts++) {
        HTTPClient http;
        http.begin(client, url);
        String authorization = "Bearer " + accessToken;
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", authorization);

        if (body.isEmpty() && (method == "PUT" || method == "POST")) {
            body = "{}";
            http.addHeader("Content-Length", String(body.length()));
        }

        if (method == "PUT") {
            result.httpCode = http.PUT(body);
        } else if (method == "POST") {
            result.httpCode = http.POST(body);
        } else if (method == "GET") {
            result.httpCode = http.GET();
        } else {
            LOG("[SpotifyClient] Unsupported HTTP method.");
            http.end();
            break;
        }

        if (result.httpCode == 401 && attempts == 0) {
            LOG("[SpotifyClient] Access token expired mid-call. Refreshing...");
            tokenValid = false;
            http.end();
            if (!EnsureTokenFresh()) {
                LOG("[SpotifyClient] Unable to refresh token after 401. Aborting call.");
                return result;
            }
            continue; // Retry with new token
        }

        if (result.httpCode > 0) {
            if (http.getSize() > 0) {
                result.payload = http.getString();
            }
            http.end();
            break; // exit the loop
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

int SpotifyClient::DownloadFile(String url, uint8_t* buffer, size_t maxSize) {
    // Use the class's built-in secure client and private certificate
    client.setCACert(digicert_root_ca);

    HTTPClient http;
    http.begin(client, url);
    // Some servers require a user-agent header
    http.addHeader("User-Agent", "ESP32-Arduino-Spotify-Client/1.0");

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.println("[SpotifyClient] DownloadFile GET failed, error: " + String(code));
        http.end();
        return 0; // Return 0 bytes on failure
    }

    WiFiClient& stream = http.getStream();
    size_t count = 0;
    unsigned long start = millis();

    // 5 second timeout
    while ((millis() - start) < 5000 && (http.connected() || stream.available())) {
        while (stream.available() && count < maxSize) {
            buffer[count++] = stream.read();
        }
        delay(1);
    }

    http.end();
    return count; // Return the number of bytes read
}
