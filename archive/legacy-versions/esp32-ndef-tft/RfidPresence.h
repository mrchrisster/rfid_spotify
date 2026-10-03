#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
// Only the hardware task owns this state. A reader fault is not card removal.
class RfidPresence {
  uint8_t uid[10] = {}, size = 0;
  bool missingStarted = false;
  uint32_t missingSince = 0;
public:
  static constexpr uint32_t RemovalMs = 1500;
  void uncertain() { missingStarted = false; }
  bool seen(const uint8_t* value, size_t length) {
    uncertain();
    if (!length || length > sizeof(uid)) return false;
    if (size == length && !memcmp(uid,value,length)) return false;
    memcpy(uid,value,length); size=length; return true;
  }
  // Call only for an actual RF timeout; health errors/selection errors never count.
  bool missing(uint32_t now, bool readerHealthy) {
    if (!readerHealthy) { uncertain(); return false; }
    if (!size) return false;
    if (!missingStarted) { missingStarted=true; missingSince=now; return false; }
    if (uint32_t(now-missingSince)<RemovalMs) return false;
    size=0; uncertain(); return true;
  }
};
