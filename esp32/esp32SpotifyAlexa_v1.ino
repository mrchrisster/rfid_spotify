#include <Arduino.h>
#include <WiFi.h>
#include "SpotifyClient.h"
#include <ArduinoJson.h>
#include <SPI.h>
#include "MFRC522.h"
#include "settings.h"

#define RST_PIN 4  // Configurable, see typical pin layout above
#define SS_PIN 5   // Configurable, see typical pin layout above

MFRC522 mfrc522(SS_PIN, RST_PIN);  // Create MFRC522 instance

// Debug Mode
bool debugMode = false;
SpotifyClient spotify = SpotifyClient(clientId, clientSecret, deviceName, refreshToken);

void setup() {
  Serial.begin(115200);
  Serial.println("Setup started");

  connectWifi();

  // Init SPI bus and MFRC522 for NFC reader
  SPI.begin();
  mfrc522.PCD_Init();

  // Refresh Spotify Auth token and Device ID (only if not in debug mode)
  if (!debugMode) {
    spotify.FetchToken();
    spotify.GetDevices();
  }
}

void loop() {
    if (mfrc522.PICC_IsNewCardPresent()) {
        Serial.println("NFC tag present");
        readNFCTag();
    }
}


void logError(String message, int httpCode) {
  Serial.println("Error: " + message + " HTTP Code: " + String(httpCode));
}

void readNFCTag() {
  if (mfrc522.PICC_ReadCardSerial()) {
    Serial.println("Card detected. Reading data...");

    String context_uri = readFromCard();
    mfrc522.PICC_HaltA();  // Halt the card
    mfrc522.PCD_StopCrypto1(); // Stop encryption on the card

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

  byte block = 4;  // Start block for reading
  String result = "";
  byte buffer[18];
  byte size = sizeof(buffer);
  bool hasData = false; // Flag to check if meaningful data is found

  while (block < 64) {  // Loop through data blocks
    if (block == 7 || block == 11) {
      block++; // Skip trailer blocks
      continue;
    }

    if (authenticateBlock(block, &key) != MFRC522::STATUS_OK) {
      Serial.print("Authentication failed at block ");
      Serial.println(block);
      break;
    }

    if (mfrc522.MIFARE_Read(block, buffer, &size) != MFRC522::STATUS_OK) {
      Serial.print("Read failed at block ");
      Serial.println(block);
      break;
    }

    // Check for non-zero data
    for (byte i = 0; i < 16; i++) {
      if (buffer[i] != 0) {
        hasData = true;
        result += (char)buffer[i]; // Append valid characters
      }
    }

    if (!hasData) {
      break;
    }

    hasData = false; // Reset flag for next block
    block++;
  }

  if (!result.startsWith("spotify:")) {
    result = "spotify:" + result;
  }

  return result;
}

MFRC522::StatusCode authenticateBlock(byte block, MFRC522::MIFARE_Key *key) {
  MFRC522::StatusCode status;
  for (int retries = 0; retries < 3; retries++) { // Retry up to 3 times
    status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, key, &(mfrc522.uid));
    if (status == MFRC522::STATUS_OK) break;
  }
  return status;
}

void playSpotifyUri(String contextUri) {
    Serial.println("Attempting to play Spotify URI: " + contextUri);

    if (debugMode) {
        Serial.println("[DEBUG] Would attempt to play: " + contextUri);
        return;
    }

    disableShuffle();
    int retryCount = 0;
    const int maxRetries = 3;

    while (retryCount < maxRetries) {
        Serial.print("Retry count: ");
        Serial.println(retryCount);

        int code = spotify.Play(contextUri);

        if (code == 200 || code == 204) {
            Serial.println("Playback started successfully. Retry count: " + String(retryCount));
            break; // Exit the loop after successful playback
        } else if (code == 401) {
            Serial.println("Access token expired. Refreshing token...");
            spotify.FetchToken(); // Refresh the access token
        } else if (code == 502) {
            Serial.println("Bad gateway. Retrying...");
        } else {
            logError("Unexpected error occurred.", code);
            break; // Exit on other errors
        }

        retryCount++;
        delay(2000); // Wait before retrying
    }

    if (retryCount >= maxRetries) {
        Serial.println("Max retries reached. Unable to play the URI.");
    }
}



void disableShuffle() {
    String url = "https://api.spotify.com/v1/me/player/shuffle?state=false";
    HttpResult shuffleResult = spotify.CallAPI("PUT", url, "{}"); // Pass an empty JSON body

    if (shuffleResult.httpCode == 200 || shuffleResult.httpCode == 204) {
        Serial.println("Shuffle disabled successfully.");
    } else {
        logError("Failed to disable shuffle.", shuffleResult.httpCode);
    }
}


void refreshTokenAndRetry(String context_uri) {
  Serial.println("Error: Auth token expired. Refreshing token...");
  spotify.FetchToken();
  int retryCode = spotify.Play(context_uri);
  if (retryCode == 200 || retryCode == 204) {
    Serial.println("Playback started successfully after refreshing token.");
  } else {
    logError("Failed to start playback after refreshing token.", retryCode);
  }
}

void playRandomAlbumFromArtist(String artistUri) {
  Serial.println("Fetching albums for artist: " + artistUri);

  if (debugMode) {
    Serial.println("[DEBUG] Would fetch albums for artist URI: " + artistUri);
    return;
  }

  String artistId = artistUri.substring(15);
  String url = "https://api.spotify.com/v1/artists/" + artistId + "/albums?include_groups=album";
  HttpResult result = spotify.CallAPI("GET", url, "");

  if (result.httpCode == 200 || result.httpCode == 204) {
    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, result.payload);

    if (error) {
      logError("JSON parsing failed.", result.httpCode);
      return;
    }

    JsonArray albums = doc["items"].as<JsonArray>();
    std::vector<String> albumUris;

    for (JsonObject album : albums) {
      String uri = album["uri"].as<String>();
      albumUris.push_back(uri);
    }

    if (!albumUris.empty()) {
      int randomIndex = random(0, albumUris.size());
      String randomAlbumUri = albumUris[randomIndex];
      Serial.println("Selected Random Album URI: " + randomAlbumUri);
      playSpotifyUri(randomAlbumUri);
    } else {
      Serial.println("No albums found for this artist.");
    }
  } else {
    logError("Failed to fetch albums.", result.httpCode);
  }
}

void connectWifi() {
  if (debugMode) {
    Serial.println("[DEBUG] WiFi connection skipped in debug mode.");
    return;
  }

  WiFi.begin(ssid, pass);
  Serial.println("");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}
