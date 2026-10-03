#pragma once
#include <string>
#include <stdint.h>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <type_traits>
class String {
  std::string value;
public:
  String()=default;
  String(const char* s):value(s?s:""){}
  String(const std::string& s):value(s){}
  template<class T,typename std::enable_if<std::is_arithmetic<T>::value,int>::type=0> String(T n):value(std::to_string(n)){}
  const char* c_str() const { return value.c_str(); }
  size_t length()const{return value.size();}
  bool isEmpty()const{return value.empty();}
  char operator[](size_t i)const{return value[i];}
  String& operator=(const char* s){value=s?s:"";return *this;}
  String& operator+=(char c){value+=c;return *this;}
  String& operator+=(const String& s){value+=s.value;return *this;}
  bool concat(const char* s){value+=s;return true;}
  bool startsWith(const char* s)const{return value.rfind(s,0)==0;}
  int indexOf(const char* s)const{auto at=value.find(s);return at==std::string::npos?-1:int(at);}
  friend bool operator==(const String&a,const String&b){return a.value==b.value;}
  friend bool operator!=(const String&a,const String&b){return !(a==b);}
  friend String operator+(const String&a,const String&b){return String(a.value+b.value);}
};
extern uint32_t testMillis;
inline uint32_t millis(){return testMillis;}
class Stream {
public:
  virtual ~Stream()=default;
  virtual int available()=0; virtual int read()=0;virtual int peek()=0;virtual void flush()=0;
  virtual size_t write(uint8_t)=0;virtual size_t write(const uint8_t*,size_t)=0;
};
