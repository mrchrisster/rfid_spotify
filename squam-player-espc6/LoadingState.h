#pragma once
#include <stdint.h>
struct LoadingState {
 uint8_t error=0;
 bool active=false,failed=false,drawn=false;
 uint32_t started=0,job=0,generation=0,frameAt=0;
 void begin(uint32_t now){active=true;failed=false;error=0;drawn=false;started=now;job=0;++generation;frameAt=now-120;}
 void stop(){active=false;drawn=false;}
 bool accepts(uint32_t id) const{return !job || int32_t(id-job)>=0;}
 void fail(uint32_t id,uint32_t now,uint8_t reason=3){if(active && (!id || id==job)){failed=true;error=reason;drawn=false;started=now;}}
 bool expired(uint32_t now) const{return active && uint32_t(now-started)>=(failed?5000U:90000U);}
 bool frameDue(uint32_t now) const{return active && !failed && uint32_t(now-frameAt)>=120;}
};
