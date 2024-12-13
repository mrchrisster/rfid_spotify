#include <SPI.h>
#include <MFRC522.h>

#define RST_PIN 4  // Reset pin
#define SS_PIN 5   // Slave select pin

MFRC522 mfrc522(SS_PIN, RST_PIN);  // Create MFRC522 instance

String inputBuffer = "";       // Buffer to store Serial input
bool cardDetected = false;     // Track card presence
unsigned long lastCheckTime = 0; // Last time the card presence was checked
const unsigned long checkInterval = 500; // Check card presence every 500ms
const int DEBOUNCE_COUNT = 3;  // Number of consecutive checks to confirm card removal
int cardAbsentCount = 0;       // Counter for consecutive absent detections

void setup() {
  Serial.begin(115200);
  SPI.begin();        // Init SPI bus
  mfrc522.PCD_Init(); // Init MFRC522
  Serial.println("Place your card near the reader...");
  Serial.println("Please paste Spotify URI (e.g., spotify:track:799ZxMkKLLWdxpXdpogkDi) and press Enter:");
}

void loop() {
  // Check for new Serial input
  if (Serial.available() > 0) {
    char receivedChar = Serial.read();
    if (receivedChar == '\n') {  // End of input
      handleSerialInput();
    } else {
      inputBuffer += receivedChar;  // Add to input buffer
    }
  }

  // Check card presence at defined intervals
  if (millis() - lastCheckTime > checkInterval) {
    lastCheckTime = millis();
    checkCardPresence();
  }
}

void handleSerialInput() {
  Serial.println("Data received: " + inputBuffer);

  // Remove the "spotify:" prefix if present
  String dataToWrite = inputBuffer;
  if (dataToWrite.startsWith("spotify:")) {
    dataToWrite = dataToWrite.substring(8);  // Remove "spotify:" prefix
  }

  Serial.println("Data to write (without prefix): " + dataToWrite);

  if (cardDetected) {
    if (writeToCard(dataToWrite)) {
      Serial.println("Data written successfully!");

      // Read the data back from the card
      String readData = readFromCard();
      if (!readData.isEmpty()) {
        String fullUri = "spotify:" + readData;  // Reconstruct full URI
        Serial.println("Read data: " + fullUri);
      } else {
        Serial.println("Failed to read data back from the card.");
      }
    } else {
      Serial.println("Failed to write data.");
    }
    mfrc522.PICC_HaltA();  // Halt the card
    mfrc522.PCD_StopCrypto1(); // Stop encryption
  } else {
    Serial.println("No card detected. Place a card near the reader and try again.");
  }

  // Clear the input buffer for the next input
  inputBuffer = "";
}

MFRC522::StatusCode authenticateBlock(byte block, MFRC522::MIFARE_Key *key) {
  MFRC522::StatusCode status;
  for (int retries = 0; retries < 3; retries++) { // Retry up to 3 times
    status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, key, &(mfrc522.uid));
    if (status == MFRC522::STATUS_OK) break;
  }
  return status;
}

void checkCardPresence() {
  static unsigned long cardDetectedTime = 0; // Last time a card was detected
  static unsigned long cardRemovedTime = 0; // Last time a card was removed

  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    if (!cardDetected) {
      cardDetectedTime = millis();
      if (millis() - cardRemovedTime > 200) { // Small debounce after removal
        Serial.println("Card detected.");
        printCardDetails();
        cardDetected = true;
      }
    }
    cardAbsentCount = 0; // Reset absent count
  } else {
    if (cardDetected) {
      cardAbsentCount++;
      if (cardAbsentCount >= DEBOUNCE_COUNT) { // Debounce removal
        cardRemovedTime = millis();
        Serial.println("Card removed.");
        cardDetected = false;
        cardAbsentCount = 0;
      }
    }
  }
}

bool writeToCard(String data) {
  MFRC522::MIFARE_Key key;
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF; // Default key
  }

  byte block = 4;  // Start block for writing
  int length = data.length();
  byte buffer[16];

  for (int i = 0; i < length; i += 16) {
    int chunkSize = (length - i > 16) ? 16 : (length - i);
    for (int j = 0; j < 16; j++) {
      buffer[j] = (j < chunkSize) ? data[i + j] : 0; // Pad with zeros
    }

    if (authenticateBlock(block, &key) != MFRC522::STATUS_OK) {
      Serial.print("Authentication failed at block ");
      Serial.println(block);
      return false;
    }

    if (mfrc522.MIFARE_Write(block, buffer, 16) != MFRC522::STATUS_OK) {
      Serial.print("Write failed at block ");
      Serial.println(block);
      return false;
    }

    Serial.print("Data written to block ");
    Serial.println(block);

    block++;
    if (block == 7 || block == 11) {
      block++; // Skip trailer blocks
    }
  }

  return true;
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

    // Stop reading if no meaningful data is found
    if (!hasData) {
      break;
    }

    hasData = false; // Reset flag for next block
    block++;
  }

  return result;
}


void printCardDetails() {
  Serial.print("Card UID: ");
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
    Serial.print(mfrc522.uid.uidByte[i], HEX);
  }
  Serial.println();
}
