#include <Arduino.h>
#include <WiFi.h>
#include "SpotifyClient.h"
#include <ArduinoJson.h>
#include <SPI.h>
#include "MFRC522.h"
#include "settings.h"

// NFC reader pins
#define RST_PIN 4
#define SS_PIN 5
MFRC522 mfrc522(SS_PIN, RST_PIN); // Create MFRC522 instance

bool debugMode = false;
bool reconnecting = false;
bool wasWifiDisconnected = false;

SpotifyClient spotify(clientId, clientSecret, deviceName, refreshToken);

void setup() {
    Serial.begin(115200);
    Serial.println("[Main] Setup started");

    connectWifi();

    // Initialize SPI bus and NFC reader
    SPI.begin();
    mfrc522.PCD_Init();

    if (!debugMode) {
        Serial.println("[Main] Ensuring token fresh and fetching devices...");
        if (!spotify.EnsureTokenFresh()) {
            Serial.println("[Main] Initial token fetch failed. Please check credentials.");
        } else {
            spotify.GetDevices();
        }
    }
}

void loop() {
    ensureWifiConnected();

    // If Wi-Fi reconnected after a disconnect, reset Spotify state
    if (WiFi.status() == WL_CONNECTED && wasWifiDisconnected) {
        Serial.println("[Main] Wi-Fi reconnected. Resetting Spotify client...");
        spotify.ResetState();
        wasWifiDisconnected = false;
    }

    // If token is nearing expiry or invalid, ensure it's fresh
    if (!debugMode && !spotify.IsTokenValid()) {
        Serial.println("[Main] Token invalid or expired. Ensuring fresh token...");
        if (!spotify.EnsureTokenFresh()) {
            Serial.println("[Main] Failed to refresh token. Requests may fail.");
        }
    }

    // NFC tag detection and processing (unchanged)
    if (mfrc522.PICC_IsNewCardPresent()) {
        Serial.println("[Main] NFC tag detected");
        readNFCTag();
    }
}

void connectWifi() {
    if (debugMode) {
        Serial.println("[Main][DEBUG] Skipping Wi-Fi connection in debug mode.");
        return;
    }

    Serial.println("[Main] Connecting to Wi-Fi...");
    WiFi.begin(ssid, pass);

    unsigned long startAttemptTime = millis();
    const unsigned long timeout = 30000; // 30 seconds timeout

    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < timeout) {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[Main] Connected to Wi-Fi.");
        Serial.print("[Main] IP Address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\n[Main] Failed to connect to Wi-Fi.");
    }
}

void ensureWifiConnected() {
    static unsigned long lastReconnectAttempt = 0;
    const unsigned long reconnectInterval = 5000; // Retry every 5 seconds

    if (WiFi.status() != WL_CONNECTED && !reconnecting) {
        if (millis() - lastReconnectAttempt > reconnectInterval) {
            reconnecting = true;
            Serial.println("[Main] Wi-Fi disconnected. Attempting to reconnect...");
            WiFi.disconnect(); // Reset connection
            WiFi.begin(ssid, pass);
            lastReconnectAttempt = millis();
        }
    } else if (WiFi.status() == WL_CONNECTED) {
        reconnecting = false;
    } else {
        wasWifiDisconnected = true;
    }
}

void logError(String message, int httpCode) {
    Serial.println("[Error] " + message + " HTTP Code: " + String(httpCode));
}

void readNFCTag() {
    if (mfrc522.PICC_ReadCardSerial()) {
        Serial.println("Card detected. Reading data...");
        
        // Read the content from the NFC card
        String context_uri = readFromCard();

        // Halt and reset the card state
        mfrc522.PICC_HaltA();  
        mfrc522.PCD_StopCrypto1(); 
        delay(50); // Ensure proper reset timing

        if (!context_uri.isEmpty()) {
            Serial.println("Read NFC tag: " + context_uri);

            // Determine the type and play the appropriate content
            if (context_uri.startsWith("spotify:artist:")) {
                playRandomAlbumFromArtist(context_uri);
            } else {
                playSpotifyUri(context_uri); // Default for anything else
            }
        } else {
            Serial.println("Failed to read data from the card.");
        }
    } else {
        Serial.println("Failed to read card serial.");
    }
}

String readFromCard() {
    MFRC522::MIFARE_Key key;
    for (byte i = 0; i < 6; i++) {
        key.keyByte[i] = 0xFF; // Default key
    }

    byte block = 4; // Start block for reading
    String result = "";
    byte buffer[18];
    byte size = sizeof(buffer);
    bool hasData = false;

    while (block < 64) {
        if (block == 7 || block == 11) { // Skip trailer blocks
            block++;
            continue;
        }

        Serial.print("[Main] Authenticating block ");
        Serial.println(block);

        if (authenticateBlock(block, &key) != MFRC522::STATUS_OK) {
            Serial.print("[Main] Authentication failed at block ");
            Serial.println(block);
            break;
        }

        if (mfrc522.MIFARE_Read(block, buffer, &size) != MFRC522::STATUS_OK) {
            Serial.print("[Main] Read failed at block ");
            Serial.println(block);
            break;
        }

        Serial.print("[Main] Data in block ");
        Serial.print(block);
        Serial.print(": ");
        for (byte i = 0; i < size; i++) {
            Serial.print((char)buffer[i]);
        }
        Serial.println();

        // Append non-zero data
        for (byte i = 0; i < 16; i++) {
            if (buffer[i] != 0) {
                hasData = true;
                result += (char)buffer[i];
            }
        }

        if (!hasData) {
            break;
        }

        hasData = false; // Reset flag
        block++;
    }

    if (!result.startsWith("spotify:")) {
        result = "spotify:" + result;
    }

    return result;
}


MFRC522::StatusCode authenticateBlock(byte block, MFRC522::MIFARE_Key *key) {
    MFRC522::StatusCode status;
    for (int retries = 0; retries < 3; retries++) {
        status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, key, &(mfrc522.uid));
        if (status == MFRC522::STATUS_OK) break;
    }
    return status;
}

void playSpotifyUri(String contextUri) {
    Serial.println("[Main] Attempting to play Spotify URI: " + contextUri);

    if (debugMode) {
        Serial.println("[Main][DEBUG] Simulated playback for URI: " + contextUri);
        return;
    }

    disableShuffle();
    int retryCount = 0;
    const int maxRetries = 3;

    while (retryCount < maxRetries) {
        Serial.print("[Main] Playback attempt: ");
        Serial.println(retryCount + 1);

        int code = spotify.Play(contextUri);

        if (code == 200 || code == 204) {
            Serial.println("[Main] Playback started successfully.");
            break;
        } else if (code == 401 || code == 404) {
            Serial.println("[Main] Access token expired or invalid device ID. Resetting state...");
            spotify.ResetState();
        } else {
            logError("Unexpected playback error.", code);
        }

        retryCount++;
        delay(2000);
    }
}

void disableShuffle() {
    HttpResult shuffleResult = spotify.CallAPI("PUT", "https://api.spotify.com/v1/me/player/shuffle?state=false", "{}");

    if (shuffleResult.httpCode == 200 || shuffleResult.httpCode == 204) {
        Serial.println("[Main] Shuffle disabled successfully.");
    } else {
        logError("Failed to disable shuffle.", shuffleResult.httpCode);
    }
}

void playRandomAlbumFromArtist(String artistUri) {
    Serial.println("[Main] Fetching albums for artist: " + artistUri);

    if (debugMode) {
        Serial.println("[Main][DEBUG] Simulated album fetch for artist URI: " + artistUri);
        return;
    }

    String artistId = artistUri.substring(15);
    HttpResult result = spotify.CallAPI("GET", "https://api.spotify.com/v1/artists/" + artistId + "/albums?include_groups=album", "");

    if (result.httpCode == 200) {
        DynamicJsonDocument doc(8192);
        DeserializationError error = deserializeJson(doc, result.payload);

        if (error) {
            logError("JSON parsing failed.", result.httpCode);
            return;
        }

        JsonArray albums = doc["items"].as<JsonArray>();
        if (!albums.isNull() && albums.size() > 0) {
            int randomIndex = random(0, albums.size());
            String randomAlbumUri = albums[randomIndex]["uri"].as<String>();
            Serial.println("[Main] Selected album URI: " + randomAlbumUri);
            playSpotifyUri(randomAlbumUri);
        } else {
            Serial.println("[Main] No albums found for artist.");
        }
    } else {
        logError("Failed to fetch albums.", result.httpCode);
    }
}
