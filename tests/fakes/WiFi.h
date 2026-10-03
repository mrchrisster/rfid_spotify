#pragma once
constexpr int WL_CONNECTED=3;
struct FakeWiFi { int state=WL_CONNECTED; int status()const{return state;} };
extern FakeWiFi WiFi;
