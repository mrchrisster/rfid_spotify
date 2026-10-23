#pragma once
#include <stdint.h>
#include <stddef.h>
#include <ctype.h>
namespace WifiSetupPolicy {
inline bool credentials(const char* ssid, size_t ssidLength, const char* password, size_t passwordLength) {
  if (!ssid || !password || !ssidLength || ssidLength>32 || passwordLength>64) return false;
  for(size_t i=0;i<ssidLength;++i) if(!ssid[i]) return false;
  for(size_t i=0;i<passwordLength;++i) if(!password[i]) return false;
  if (!passwordLength) return true;
  if (passwordLength==64) {
    for(size_t i=0;i<64;++i) if(!isxdigit(static_cast<unsigned char>(password[i]))) return false;
    return true;
  }
  return passwordLength>=8;
}
struct Trial {
  uint32_t started=0, connectedAt=0;
  bool observed=false;
  void begin(uint32_t now) { started=now; observed=false; }
  // DHCP and a stable association are checked, not Spotify or internet service.
  bool ready(uint32_t now, bool connected) {
    if(!connected) { observed=false; return false; }
    if(!observed) { observed=true;connectedAt=now; }
    return uint32_t(now-connectedAt)>=3000;
  }
  bool expired(uint32_t now) const { return uint32_t(now-started)>=30000; }
};
}
