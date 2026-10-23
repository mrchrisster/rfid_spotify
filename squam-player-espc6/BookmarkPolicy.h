#pragma once
#include <stdint.h>
// Observe Spotify frequently in RAM; persist less often to reduce NVS wear.
// Explicit transitions and controlled shutdowns bypass the periodic interval.
namespace BookmarkPolicy {
constexpr uint32_t CaptureMs=30000;
constexpr uint32_t SaveMs=5*60*1000;
inline bool captureDue(uint32_t now,uint32_t last){return uint32_t(now-last)>=CaptureMs;}
inline bool saveDue(uint32_t now,uint32_t last){return uint32_t(now-last)>=SaveMs;}
}
