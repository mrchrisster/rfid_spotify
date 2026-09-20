#pragma once
#include <stdint.h>
namespace RfidRecovery {
struct Registers {
  uint8_t version, antenna, timer, prescaler, command;
  bool healthy() const {
    bool known=version==0x90 || version==0x91 || version==0x92 || version==0x88 || version==0x12;
    return known && (antenna & 3)==3 && timer==0x80 && prescaler==0xa9;
  }
};
// Our code owns NRSTPD. Disable the library's reset-pin management, which can
// otherwise leave it INPUT and turn subsequent LOW/HIGH writes into pull changes.
template<class Reader, class Pins>
void reset(Reader& reader, Pins& pins, uint8_t selectPin, uint8_t resetPin) {
  pins.output(resetPin);
  pins.write(resetPin,false); pins.wait(50);
  pins.write(resetPin,true); pins.wait(50);
  reader.PCD_Init(selectPin,Reader::UNUSED_PIN);
}
}
