#pragma once
#include "Arduino.h"
struct IPAddress {
 uint32_t value=0;
 IPAddress()=default;
 IPAddress(unsigned a,unsigned b,unsigned c,unsigned d):value(a|(b<<8)|(c<<16)|(d<<24)){}
 operator uint32_t() const{return value;}
 String toString() const{return String(value&255)+"."+String((value>>8)&255)+"."+String((value>>16)&255)+"."+String(value>>24);}
};
constexpr int WIFI_STA=1,WIFI_AP_STA=3,WL_CONNECTED=3,WIFI_SCAN_RUNNING=-1;
struct FakeWiFi {
 bool connected=false,ap=false,persist=true,autoReconnect=true;
 int scans=-2,begins=0;String name,password;
 void persistent(bool b){persist=b;}void mode(int){}void setAutoReconnect(bool b){autoReconnect=b;}
 void disconnect(bool,bool){connected=false;}
 void begin(const char* n,const char* p){name=n;password=p;++begins;connected=false;}
 int status(){return connected?WL_CONNECTED:0;}
 IPAddress localIP(){return connected?IPAddress(192,168,0,25):IPAddress();}
 String SSID(){return name;}String SSID(int){return "Test network";}int RSSI(int){return -50;}
 bool softAPConfig(IPAddress,IPAddress,IPAddress){return true;}
 bool softAP(const char*,const char*,int,bool,int){ap=true;return true;}
 void softAPdisconnect(bool){ap=false;}
 void scanDelete(){scans=-2;}int scanComplete(){return scans;}void scanNetworks(bool){scans=WIFI_SCAN_RUNNING;}
};
inline FakeWiFi WiFi;
