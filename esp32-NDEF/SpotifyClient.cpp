#include "SpotifyClient.h"
#include <HTTPClient.h>
#include <base64.h>
#include <ArduinoJson.h>

SpotifyClient::SpotifyClient(String clientId, String clientSecret, String deviceName, String refreshToken) {
    this->clientId = clientId;
    this->clientSecret = clientSecret;
    this->deviceName = deviceName;
    this->refreshToken = refreshToken;

    client.setCACert(digicert_root_ca);
    tokenValid = false;
    deviceId = "";
    lastTokenRefresh = 0;
    tokenExpiresIn = 0;
    // tokenRefreshInterval is initialized to a default of 3600000 (1 hour) in the header
}

void SpotifyClient::FetchToken() {
    tokenValid = false; // Mark token invalid before fetching
    Serial.println("[SpotifyClient] Fetching new token...");

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
            Serial.println("[SpotifyClient] Token fetch response: " + returnedPayload);

            if (httpCode == 200) {
                accessToken = ParseJson("access_token", returnedPayload);

                String expiresInStr = ParseJson("expires_in", returnedPayload);
                if (!expiresInStr.isEmpty()) {
                    int expiresIn = expiresInStr.toInt(); // Expiration in seconds
                    // Refresh 5 minutes early
                    tokenRefreshInterval = (expiresIn - 300) * 1000UL; 
                    lastTokenRefresh = millis();
                    tokenValid = true;
                    Serial.println("[SpotifyClient] Token refreshed successfully. Valid for " + String(expiresIn) + " seconds.");
                } else {
                    Serial.println("[SpotifyClient] Failed to parse expires_in. Using default interval.");
                    tokenValid = true; // Mark as valid but fallback to default interval
                    lastTokenRefresh = millis();
                }

                success = true;
                break;
            } else {
                Serial.print("[SpotifyClient] Failed to fetch token. HTTP Code: ");
                Serial.println(httpCode);
                Serial.println("[SpotifyClient] Response: " + returnedPayload);
            }
        } else {
            Serial.print("[SpotifyClient] Connection error: ");
            Serial.println(http.errorToString(httpCode));
        }

        Serial.println("[SpotifyClient] Retrying in 2 seconds...");
        delay(2000);
    }

    http.end();

    if (!success) {
        Serial.println("[SpotifyClient] All attempts to refresh token failed. Token remains invalid.");
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
        Serial.println("[SpotifyClient] Token is invalid or expired. Attempting refresh...");
        FetchToken();
        if (!tokenValid || IsTokenExpired()) {
            Serial.println("[SpotifyClient] Unable to obtain a fresh token.");
            return false;
        }
    }
    return true;
}

int SpotifyClient::Play(String context_uri) {
    Serial.println("[SpotifyClient] Play()");

    if (!EnsureTokenFresh()) {
        Serial.println("[SpotifyClient] Cannot play without a valid token.");
        return 401; // Unauthorized
    }

    if (deviceId.isEmpty()) {
        Serial.println("[SpotifyClient] Device ID is empty. Attempting to refresh devices...");
        GetDevices();
        if (deviceId.isEmpty()) {
            Serial.println("[SpotifyClient] Error: Unable to set deviceId. Aborting playback.");
            return 404;
        }
    }

    String body = "{\"context_uri\":\"" + context_uri + "\",\"offset\":{\"position\":0,\"position_ms\":0}}";
    String url = "https://api.spotify.com/v1/me/player/play?device_id=" + deviceId;
    HttpResult result = CallAPI("PUT", url, body);

    if (result.httpCode != 200 && result.httpCode != 204) {
        Serial.print("[SpotifyClient] Error: Unexpected HTTP Code: ");
        Serial.println(result.httpCode);
    }
    return result.httpCode;
}

int SpotifyClient::Shuffle() {
    Serial.println("[SpotifyClient] Shuffle()");
    if (!EnsureTokenFresh()) {
        Serial.println("[SpotifyClient] Cannot shuffle without a valid token.");
        return 401; 
    }
    HttpResult result = CallAPI("PUT", "https://api.spotify.com/v1/me/player/shuffle?state=true&device_id=" + deviceId, "{}");
    return result.httpCode;
}

int SpotifyClient::Next() {
    Serial.println("[SpotifyClient] Next()");
    if (!EnsureTokenFresh()) {
        Serial.println("[SpotifyClient] Cannot skip track without a valid token.");
        return 401; 
    }
    HttpResult result = CallAPI("POST", "https://api.spotify.com/v1/me/player/next?device_id=" + deviceId, "{}");
    return result.httpCode;
}

String SpotifyClient::GetDevices() {
    if (!EnsureTokenFresh()) {
        Serial.println("[SpotifyClient] Cannot fetch devices without a valid token.");
        return "";
    }

    const int maxRetries = 3;
    const int retryDelay = 2000;
    String foundDeviceId = "";

    for (int attempt = 1; attempt <= maxRetries; attempt++) {
        Serial.printf("[SpotifyClient] Fetching devices (Attempt %d)...\n", attempt);

        HttpResult result = CallAPI("GET", "https://api.spotify.com/v1/me/player/devices", "");
        if (result.httpCode == 200) {
            Serial.println("[SpotifyClient] Devices response: " + result.payload);
            foundDeviceId = GetDeviceId(result.payload);
            if (!foundDeviceId.isEmpty()) {
                deviceId = foundDeviceId;
                Serial.println("[SpotifyClient] Found device ID: " + foundDeviceId);
                return foundDeviceId;
            } else {
                Serial.println("[SpotifyClient] Device not found. Retrying...");
            }
        } else {
            Serial.print("[SpotifyClient] Failed to fetch devices. HTTP Code: ");
            Serial.println(result.httpCode);
            Serial.println("Response: " + result.payload);
        }

        delay(retryDelay);
    }

    Serial.println("[SpotifyClient] Max retries reached. Device not found.");
    return foundDeviceId;
}

HttpResult SpotifyClient::CallAPI(String method, String url, String body) {
    HttpResult result;
    result.httpCode = 0;
    result.payload = "";

    // Ensure we have a fresh token before making the call
    if (!EnsureTokenFresh()) {
        Serial.println("[SpotifyClient] Cannot call API without a valid token.");
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
            Serial.println("[SpotifyClient] Unsupported HTTP method.");
            http.end();
            break;
        }

        if (result.httpCode == 401 && attempts == 0) {
            Serial.println("[SpotifyClient] Access token expired mid-call. Refreshing...");
            tokenValid = false;
            http.end();
            if (!EnsureTokenFresh()) {
                Serial.println("[SpotifyClient] Unable to refresh token after 401. Aborting call.");
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
            Serial.print("[SpotifyClient] Failed to connect to URL: ");
            Serial.println(url);
            http.end();
        }
    }

    return result;
}

void SpotifyClient::ResetState() {
    Serial.println("[SpotifyClient] Resetting Spotify client state...");
    EnsureTokenFresh();
    GetDevices();
}

String SpotifyClient::GetDeviceId(String json) {
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        Serial.print("[SpotifyClient] JSON parsing failed: ");
        Serial.println(error.c_str());
        return "";
    }

    JsonArray devices = doc["devices"].as<JsonArray>();
    for (JsonObject device : devices) {
        String name = device["name"].as<String>();
        String id = device["id"].as<String>();

        Serial.print("[SpotifyClient] Device name: ");
        Serial.print(name);
        Serial.print(", ID: ");
        Serial.println(id);

        if (name == deviceName) {
            return id;
        }
    }

    Serial.println("[SpotifyClient] " + deviceName + " device name not found.");
    return "";
}

String SpotifyClient::ParseJson(String key, String json) {
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        Serial.print("[SpotifyClient] JSON parsing failed: ");
        Serial.println(error.c_str());
        return "";
    }

    return doc[key] | "";
}
