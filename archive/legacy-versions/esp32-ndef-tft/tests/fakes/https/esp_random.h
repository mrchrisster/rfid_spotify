#pragma once
#include <stddef.h>
#include <stdint.h>
inline void esp_fill_random(void* p,size_t n){static uint32_t counter=1;auto* b=static_cast<uint8_t*>(p);while(n--){counter=counter*1664525+1013904223;*b++=counter>>24;}}
