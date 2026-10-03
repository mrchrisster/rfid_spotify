#pragma once
#include <stdint.h>
namespace StartupPolicy {
inline bool authDue(uint32_t now,uint32_t last,bool wifi,bool clock,bool token,uint32_t cooldown) {
 return wifi && clock && !cooldown && uint32_t(now-last)>=(token?30000U:1000U);
}
inline bool waitForAuth(bool wifi,bool clock,bool token,bool revoked,int error) {
 return !wifi || !clock || (!token && !revoked && error!=400);
}
}
