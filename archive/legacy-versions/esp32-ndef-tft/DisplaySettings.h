#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace DisplaySettings {
constexpr uint32_t StartupSeconds=30*60;
constexpr uint32_t DefaultSeconds=600, MaxSeconds=86400, DebugBit=0x80000000UL;
inline bool validSeconds(uint32_t seconds) { return seconds==0 || (seconds>=10 && seconds<=MaxSeconds); }
inline uint32_t pack(bool debug,uint32_t seconds) { return (debug?DebugBit:0)|seconds; }
inline bool debug(uint32_t settings) { return settings&DebugBit; }
inline uint32_t seconds(uint32_t settings) { return settings&~DebugBit; }
inline bool expired(uint32_t now,uint32_t shownAt,uint32_t seconds) {
  return seconds && uint32_t(now-shownAt)>=seconds*1000UL;
}
// A bounded POD snapshot crosses the task queue; no shared Arduino Strings.
struct LogSnapshot {
  static constexpr size_t Rows=18, Columns=52;
  char lines[Rows][Columns+1]={};
  uint32_t revision=0;
  void append(const char* input) {
    if(!input || !*input)return;
    while(*input) {
      memmove(lines,lines+1,(Rows-1)*sizeof(lines[0]));
      memset(lines[Rows-1],0,sizeof(lines[0]));
      size_t n=0;
      while(*input && *input!='\n' && n<Columns) {
        unsigned char c=static_cast<unsigned char>(*input++);
        lines[Rows-1][n++]=(c>=32 && c<127)?char(c):'?';
      }
      if(*input=='\n')++input;
      ++revision;
    }
  }
};
}
