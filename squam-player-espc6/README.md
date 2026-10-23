# Squam Player — ESP32-C6 + MFRC522

Standalone Arduino sketch for the ESP32-C6 headless player using the MFRC522 SPI reader. Folder and sketch names match for Arduino IDE.

## Wiring

| MFRC522 | ESP32-C6 |
|---|---:|
| VCC | 3.3V |
| GND | GND |
| SDA / SS | GPIO 11 |
| RST | GPIO 10 |
| SCK | GPIO 1 |
| MOSI | GPIO 7 |
| MISO | GPIO 6 |

`SDA` on MFRC522 boards is SPI chip select, not I²C SDA. Keep reader signals at 3.3V. This pin map follows the device wiring confirmed by the user on 2026-10-22.

## Build

1. Install ESP32 Arduino core 3.3.11 and ArduinoJson 7.4.3.
2. Open `squam-player-espc6.ino` with Arduino IDE.
3. Select **ESP32C6 Dev Module** and a partition scheme with OTA app slots (Minimal SPIFFS).
4. Add a local `secrets.h` based on `secrets.example.h`; provide device credentials/certificate locally. Never commit those files.
5. Compile and upload over USB for first installation. Later app updates are available through the player web UI.

The default profile in `HardwareProfile.h` is C6 + MFRC522 headless. TFT is disabled.
