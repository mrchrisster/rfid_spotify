#define CORE_DEBUG_LEVEL ARDUHAL_LOG_LEVEL_VERBOSE

#include <HTTPClient.h>
#include "SpotifyClient.h"
#include <base64.h>
#include <ArduinoJson.h>

SpotifyClient::SpotifyClient(String clientId, String clientSecret, String deviceName, String refreshToken) {
    this->clientId = clientId;
    this->clientSecret = clientSecret;
    this->refreshToken = refreshToken;
    this->deviceName = deviceName;

    client.setCACert(digicert_root_ca);
    tokenValid = false;
    deviceId = "";
}

void SpotifyClient::FetchToken() {
    tokenValid = false; // Mark token as invalid before fetching
    Serial.println("Fetching new token...");

    HTTPClient http;
    String body = "grant_type=refresh_token&refresh_token=" + refreshToken;
    String authorizationRaw = clientId + ":" + clientSecret;
    String authorization = base64::encode(authorizationRaw);

    http.begin(client, "https://accounts.spotify.com/api/token");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("Authorization", "Basic " + authorization);

    int attempts = 0;
    const int maxAttempts = 3;

    while (attempts < maxAttempts) {
        int httpCode = http.POST(body);

        if (httpCode > 0) {
            String returnedPayload = http.getString();
            Serial.println("Token fetch response: " + returnedPayload);

            if (httpCode == 200) {
                accessToken = ParseJson("access_token", returnedPayload);
                Serial.println("Got new access token: " + accessToken);
                tokenValid = true; // Mark token as valid
                http.end();
                return;
            } else {
                Serial.println("Failed to fetch new access token.");
                Serial.print("HTTP Code: ");
                Serial.println(httpCode);
                Serial.println("Response: " + returnedPayload);
            }
        } else {
            Serial.println("Connection to Spotify token endpoint failed.");
        }

        attempts++;
        delay(2000); // Wait before retrying
    }

    Serial.println("Max retries reached. Token fetch failed.");
    tokenValid = false; // Mark token as invalid
    http.end();
}

int SpotifyClient::Shuffle() {
    Serial.println("Shuffle()");
    HttpResult result = CallAPI("PUT", "https://api.spotify.com/v1/me/player/shuffle?state=true&device_id=" + deviceId, "{}");
    return result.httpCode;
}

int SpotifyClient::Next() {
    Serial.println("Next()");
    HttpResult result = CallAPI("POST", "https://api.spotify.com/v1/me/player/next?device_id=" + deviceId, "{}");
    return result.httpCode;
}

void SpotifyClient::ResetState() {
    Serial.println("Resetting Spotify client state...");
    FetchToken(); // Refresh the access token
    GetDevices(); // Retrieve the device ID
}

int SpotifyClient::Play(String context_uri) {
    Serial.println("Play()");
    String body = "{\"context_uri\":\"" + context_uri + "\",\"offset\":{\"position\":0,\"position_ms\":0}}";

    if (deviceId.isEmpty()) {
        Serial.println("Device ID is empty. Attempting to refresh devices...");
        GetDevices();
        if (deviceId.isEmpty()) {
            Serial.println("Error: Unable to set deviceId. Aborting playback.");
            return 404;
        }
    }

    String url = "https://api.spotify.com/v1/me/player/play?device_id=" + deviceId;
    HttpResult result = CallAPI("PUT", url, body);

    if (result.httpCode != 200 && result.httpCode != 204) {
        Serial.print("Error: Unexpected error occurred. HTTP Code: ");
        Serial.println(result.httpCode);
    }
    return result.httpCode;
}

HttpResult SpotifyClient::CallAPI(String method, String url, String body) {
    HttpResult result;
    result.httpCode = 0;
    result.payload = "";

    if (!tokenValid) {
        Serial.println("Token is invalid. Attempting to refresh...");
        FetchToken();
        if (!tokenValid) {
            Serial.println("Unable to refresh token. Aborting API call.");
            return result;
        }
    }

    Serial.print("Calling URL: ");
    Serial.println(url);

    HTTPClient http;
    http.begin(client, url);
    String authorization = "Bearer " + accessToken;
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", authorization);

    if (body.isEmpty() && (method == "PUT" || method == "POST")) {
        body = "{}";
        http.addHeader("Content-Length", String(body.length()));
    }

    int attempts = 0;
    const int maxAttempts = 2;

    while (attempts < maxAttempts) {
        if (method == "PUT") {
            result.httpCode = http.PUT(body);
        } else if (method == "POST") {
            result.httpCode = http.POST(body);
        } else if (method == "GET") {
            result.httpCode = http.GET();
        } else {
            Serial.println("Unsupported HTTP method.");
            break;
        }

        if (result.httpCode == 401) {
            Serial.println("Access token expired. Refreshing...");
            FetchToken();
            if (!tokenValid) {
                Serial.println("Token refresh failed. Aborting API call.");
                break;
            }
            attempts++;
            continue;
        }

        if (result.httpCode > 0) {
            if (http.getSize() > 0) {
                result.payload = http.getString();
            }
            break;
        } else {
            Serial.print("Failed to connect to URL: ");
            Serial.println(url);
        }
        attempts++;
    }

    http.end();
    return result;
}

String SpotifyClient::GetDevices() {
    const int maxRetries = 3;
    const int retryDelay = 2000;
    String foundDeviceId = "";

    for (int attempt = 1; attempt <= maxRetries; attempt++) {
        Serial.print("Fetching devices (Attempt ");
        Serial.print(attempt);
        Serial.println(")...");

        HttpResult result = CallAPI("GET", "https://api.spotify.com/v1/me/player/devices", "");

        if (result.httpCode == 200) {
            Serial.println("Devices response: " + result.payload);
            foundDeviceId = GetDeviceId(result.payload);
            if (!foundDeviceId.isEmpty()) {
                deviceId = foundDeviceId;
                Serial.print("Found device ID: ");
                Serial.println(foundDeviceId);
                return foundDeviceId;
            } else {
                Serial.println("Device not found in the response. Retrying...");
            }
        } else {
            Serial.print("Failed to fetch devices. HTTP Code: ");
            Serial.println(result.httpCode);
            Serial.println("Response: " + result.payload);
        }

        delay(retryDelay);
    }

    Serial.println("Max retries reached. Device not found.");
    return foundDeviceId;
}

String SpotifyClient::GetDeviceId(String json) {
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        Serial.print("JSON parsing failed: ");
        Serial.println(error.c_str());
        return "";
    }

    JsonArray devices = doc["devices"].as<JsonArray>();
    for (JsonObject device : devices) {
        String name = device["name"].as<String>();
        String id = device["id"].as<String>();

        Serial.print("Device name: ");
        Serial.print(name);
        Serial.print(", ID: ");
        Serial.println(id);

        if (name == deviceName) {
            return id;
        }
    }

    Serial.println(deviceName + " device name not found.");
    return "";
}

String SpotifyClient::ParseJson(String key, String json) {
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        Serial.print("JSON parsing failed: ");
        Serial.println(error.c_str());
        return "";
    }

    return doc[key] | "";
}
