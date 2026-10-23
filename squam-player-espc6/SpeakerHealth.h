#pragma once
#include <stdint.h>
// Network-owned evidence. Paused/idle is healthy; API failures are not proof of
// an absent speaker. Require two successful discovery misses to avoid flapping.
class SpeakerHealth {
public:
  enum State:uint8_t { Unknown, Available, Playing, Paused, Unavailable };
  State state=Unknown;uint8_t misses=0;
  void found(State next=Available){state=next;misses=0;}
  void missing(){if(misses<2)++misses;if(misses>=2)state=Unavailable;else if(state!=Unavailable)state=Unknown;}
  void uncertain(){misses=0;if(state!=Unavailable)state=Unknown;}
  static const char* label(uint8_t value){switch(value){case Available:return "Available";case Playing:return "Playing";case Paused:return "Paused";case Unavailable:return "Unavailable";default:return "Unknown";}}
};
