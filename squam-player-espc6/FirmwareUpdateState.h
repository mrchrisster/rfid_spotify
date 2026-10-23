#pragma once
#include <atomic>
#include <stdint.h>
#include <stddef.h>
namespace FirmwareUpdateState {
inline std::atomic<bool> requested{false},networkReady{false},hardwareReady{false};
inline std::atomic<unsigned> percent{0};
inline bool ready(){return requested && networkReady && hardwareReady;}
// ESP32 app header + first segment's app descriptor. Reject merged images,
// bootloaders and other chip families before opening the inactive app partition.
#if defined(CONFIG_IDF_TARGET_ESP32C6)
inline constexpr uint8_t ChipId=0x0d;
#else
inline constexpr uint8_t ChipId=0;
#endif
inline bool applicationHeader(const uint8_t* b,size_t size){
 return size>=36 && b[0]==0xe9 && b[1]>0 && b[1]<=16 && b[12]==ChipId && b[13]==0 &&
 b[32]==0x32 && b[33]==0x54 && b[34]==0xcd && b[35]==0xab;
}
}
