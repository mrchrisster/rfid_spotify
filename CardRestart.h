#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
// Hardware-owned consecutive presentation gesture; artist/album has no timeout.
// Presence detection must confirm removal first; held cards never reach this code.
// Other URI types retain their two-minute restart gesture.
class CardRestart {
  uint8_t uid[10]={}; size_t length=0; char content[48]={}; uint32_t at=0;
public:
  static constexpr uint32_t WindowMs = 2UL * 60UL * 1000UL;
  static bool shuffles(bool repeated,const char* uri){return repeated && uri && (!strncmp(uri,"spotify:album:",14) || !strncmp(uri,"spotify:artist:",15));}
  void clear(){length=0;}
  bool present(const uint8_t* id,size_t size,const char* uri,uint32_t now){
    bool repeated=length==size && size>0 && size<=10 && !memcmp(uid,id,size) &&
      !strcmp(content,uri) && (shuffles(true,uri) || uint32_t(now-at)<=WindowMs);
    clear();
    if(size && size<=10 && strlen(uri)<sizeof(content)){
      memcpy(uid,id,size);length=size;strcpy(content,uri);at=now;
    }
    return repeated;
  }
};
