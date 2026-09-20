#pragma once
#include "Arduino.h"
#include <map>
#include <string>
namespace FakeNvs {
inline std::map<std::string,String> values;
inline bool failWrite=false, failReadback=false;
}
class Preferences {
public:
  bool begin(const char*,bool) { return true; }
  size_t putString(const char* key,const String& value) {
    if(FakeNvs::failWrite)return 0;
    FakeNvs::values[key]=value;return value.length();
  }
  String getString(const char* key,const char* fallback) {
    if(FakeNvs::failReadback)return "damaged";
    auto it=FakeNvs::values.find(key);return it==FakeNvs::values.end()?String(fallback):it->second;
  }
};
