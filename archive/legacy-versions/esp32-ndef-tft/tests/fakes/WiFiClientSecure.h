#pragma once
#include "Arduino.h"
class WiFiClientSecure {
public:
  void stop(){}
  void setCACertBundle(const uint8_t*,size_t){}
  void setHandshakeTimeout(unsigned long){}
};
