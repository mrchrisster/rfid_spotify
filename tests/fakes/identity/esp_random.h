#pragma once
#include <stdlib.h>
inline void esp_fill_random(void* output,size_t size) { arc4random_buf(output,size); }
