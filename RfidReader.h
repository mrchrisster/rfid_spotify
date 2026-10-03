#pragma once
#include <MFRC522.h>
#include "SafeNdef.h"

class RfidReader {
  MFRC522& chip;
  bool classic = false;
  size_t capacity = 0, cached = size_t(-1);
  uint8_t cache[18] = {};
public:
  bool ioError = false;
  MFRC522::StatusCode lastStatus = MFRC522::STATUS_OK;
  const char* errorOperation = "none";
  uint8_t errorAddress = 0, responseBytes = 0;
  bool failed(const char* operation, uint8_t address, MFRC522::StatusCode status, uint8_t bytes = 0) {
    ioError = true; errorOperation = operation; errorAddress = address;
    lastStatus = status; responseBytes = bytes; return false;
  }
  explicit RfidReader(MFRC522& chip) : chip(chip) {}
  bool begin() {
    auto type = chip.PICC_GetType(chip.uid.sak);
    classic = type == MFRC522::PICC_TYPE_MIFARE_1K;
    if (classic) { capacity = 720; return true; }
    if (type != MFRC522::PICC_TYPE_MIFARE_UL) return false;
    uint8_t cc[18], count = sizeof(cc);
    auto status = chip.MIFARE_Read(3, cc, &count);
    if (status != MFRC522::STATUS_OK || count != 18) return failed("capability read", 3, status, count);
    if (cc[0] != 0xe1 || (cc[1] >> 4) != 1 || (cc[3] >> 4) != 0) return false;
    capacity = size_t(cc[2]) * 8;
    // Page addressing is one byte. CC is untrusted.
    return capacity >= 16 && capacity <= 1008;
  }
  bool read(size_t offset, uint8_t* out, size_t size) {
    if (offset > capacity || size > capacity - offset) return false;
    while (size) {
      size_t base = offset & ~size_t(15);
      // Keep the final four-page read wholly inside the data area.
      if (!classic && base + 16 > capacity) base = capacity - 16;
      if (base != cached) {
        uint8_t address;
        if (classic) {
          address = 4 + (base / 48) * 4 + (base % 48) / 16;
          MFRC522::MIFARE_Key key = {{0xd3,0xf7,0xd3,0xf7,0xd3,0xf7}};
          auto status = chip.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, address, &key, &chip.uid);
          if (status != MFRC522::STATUS_OK) return failed("authenticate", address, status);
        } else address = 4 + base / 4;
        uint8_t count = sizeof(cache);
        auto status = chip.MIFARE_Read(address, cache, &count);
        if (status != MFRC522::STATUS_OK || count != 18) return failed("data read", address, status, count);
        cached = base;
      }
      size_t available = 16 - (offset - base), copy = size < available ? size : available;
      memcpy(out, cache + (offset - base), copy); out += copy; offset += copy; size -= copy;
    }
    return true;
  }
  bool spotifyUri(char* uri, size_t size) { return begin() && SafeNdef::read(*this, capacity, uri, size); }
};
