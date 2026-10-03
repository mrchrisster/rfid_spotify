#pragma once
#include <vector>
#define U_FLASH 0
struct FakeUpdate {
 bool active=false,failBegin=false,failWrite=false,failEnd=false;size_t expected=0;unsigned aborts=0;std::vector<uint8_t> bytes;
 bool begin(size_t size,int){if(failBegin)return false;active=true;expected=size;bytes.clear();return true;}
 size_t write(uint8_t* p,size_t size){if(failWrite)return 0;bytes.insert(bytes.end(),p,p+size);return size;}
 void abort(){active=false;++aborts;}
 bool end(bool partial){assert(!partial);active=false;return !failEnd && bytes.size()==expected;}
};
inline FakeUpdate Update;
