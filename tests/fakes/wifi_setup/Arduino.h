#pragma once
#include "../Arduino.h"
#include <cstdio>
#define PROGMEM
struct FakeSerial { void println(const char*){} template<class... T> void printf(const char*,T...){} };
inline FakeSerial Serial;
struct FakeESP { uint64_t getEfuseMac(){return 0x123456789;} };
inline FakeESP ESP;
