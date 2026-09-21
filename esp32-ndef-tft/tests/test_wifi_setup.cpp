#include "WifiSetup.cpp"
#include <assert.h>
#include <stdio.h>
uint32_t testMillis=0;
void logMessage(const String&){}
namespace WifiSetup {const char* languageCode(){return "de";}}
static void advance(unsigned ms){testMillis+=ms;WifiSetup::tick();}
int main(){
 using namespace WifiSetup;
 assert(!WifiSetupPolicy::credentials("",0,"",0));
 assert(!WifiSetupPolicy::credentials("x",1,"short",5));
 assert(WifiSetupPolicy::credentials("x",1,"",0));
 assert(WifiSetupPolicy::credentials("x",1,"12345678",8));
 WifiSetupPolicy::Trial rollover;rollover.begin(UINT32_MAX-1000);
 assert(!rollover.expired(1000));assert(rollover.expired(28999));
 WebServer web;begin("old-network","old-password");routes(web);
 assert(!WiFi.persist && !WiFi.autoReconnect);
 advance(29999);assert(!active());advance(1);assert(active());
 WiFi.connected=true;advance(1);advance(9999);assert(active());
 WiFi.connected=false;advance(1);WiFi.connected=true;advance(1);advance(9999);assert(active());
 advance(1);assert(!active()); // Only a continuous ten-second recovery closes it.
 requestStart();tick();assert(active());advance(20000);assert(active()); // Manual setup stays open.
 web.incoming=IPAddress(192,168,0,25);web.call("/wifi/connect",HTTP_POST,"{}");assert(web.code==403);
 web.incoming=IPAddress(192,168,4,1);web.requestHeader="";web.call("/wifi/connect",HTTP_POST,"{}");assert(web.code==403);web.requestHeader="RFIDPlayer";
 web.call("/wifi/connect",HTTP_POST,"{\"ssid\":\"new-network\",\"password\":\"short\"}");assert(web.code==400);
 web.call("/wifi/connect",HTTP_POST,"{\"ssid\":\"new-network\",\"password\":\"new-password\"}");assert(web.code==202);
 tick();assert(state()==Testing && WiFi.name=="new-network");
 web.call("/wifi/connect",HTTP_POST,"{}");assert(web.code==409);
 assert(FakeNvs::values.count("credentials")==0);
 advance(30000);assert(state()==Failed && WiFi.name=="old-network");assert(FakeNvs::values.count("credentials")==0);
 web.call("/wifi/connect",HTTP_POST,"{\"ssid\":\"new-network\",\"password\":\"new-password\"}");tick();
 WiFi.connected=true;advance(1);advance(2999);assert(state()==Testing);
 WiFi.connected=false;advance(1);WiFi.connected=true;advance(1); // Stability timer restarts on dropout.
 FakeNvs::failWrite=true;advance(3000);assert(state()==StorageFailed && WiFi.name=="old-network");assert(FakeNvs::values.count("credentials")==0);
 FakeNvs::failWrite=false;
 web.call("/wifi/connect",HTTP_POST,"{\"ssid\":\"new-network\",\"password\":\"new-password\"}");tick();
 WiFi.connected=true;advance(1);advance(3000);assert(state()==Saved);
 assert(FakeNvs::values["credentials"].indexOf("new-network")>=0);
 web.call("/wifi/status",HTTP_GET);assert(web.response.indexOf("new-password")==-1);
 advance(30000);assert(!active());
 begin("old-network","old-password");assert(WiFi.name=="new-network"); // Saved credentials override compiled defaults.
 requestStart();tick();assert(active());
 web.call("/wifi/connect",HTTP_POST,"{\"ssid\":\"other\",\"password\":\"password\"}");tick();
 web.call("/wifi/cancel",HTTP_POST);assert(web.code==200);tick();assert(WiFi.name=="new-network");advance(1000);assert(!active());
 puts("Wi-Fi fallback, AP-only authorization, validation, trial rollback, stable commit and reboot restore passed");
}
