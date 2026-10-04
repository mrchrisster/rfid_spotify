#include <Arduino.h>
#include <ArduinoJson.h>
#include <cassert>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <iostream>
uint32_t testMillis=1;uint32_t esp_random(){return 1234;}
struct {unsigned reboots=0;void restart(){++reboots;}} ESP;
enum {HTTP_GET,HTTP_POST,UPLOAD_FILE_START,UPLOAD_FILE_WRITE,UPLOAD_FILE_END,UPLOAD_FILE_ABORTED};
struct HTTPUpload{int status=0;size_t currentSize=0;uint8_t* buf=nullptr;};
class WebServer {
public:
 struct Route{std::function<void()> end,chunk;};std::map<std::string,Route> routes;
 HTTPUpload file;String body,nonce;int code=0;String reply;
 void on(const char* path,int,std::function<void()> end,std::function<void()> chunk={}){routes[path]={end,chunk};}
 void send(int status,const char*,const String& response){code=status;reply=response;}
 String arg(const char*){return body;}String header(const char*){return nonce;}HTTPUpload& upload(){return file;}
};
bool permitted=true;bool auth(bool){return permitted;}
#include "FirmwareUpdate.h"
WebServer server;
void reset(){using namespace FirmwareUpdate;expected=received=headerSize=0;writing=uploaded=success=acceptUpload=false;session="";message="";FirmwareUpdateState::requested=false;FirmwareUpdateState::networkReady=false;FirmwareUpdateState::hardwareReady=false;Update=FakeUpdate{};permitted=true;}
void prepare(size_t size=100){server.body=String("{\"size\":")+String(size)+"}";server.routes["/api/firmware/prepare"].end();server.nonce=FirmwareUpdate::session;}
void chunk(int state,std::vector<uint8_t>& data){server.file={state,data.size(),data.data()};server.routes["/api/firmware/upload"].chunk();}
std::vector<uint8_t> binary(){std::vector<uint8_t> b(100);b[0]=0xe9;b[1]=1;b[12]=FirmwareUpdateState::ChipId;b[32]=0x32;b[33]=0x54;b[34]=0xcd;b[35]=0xab;return b;}
void start(){prepare();assert(server.code==202);FirmwareUpdateState::networkReady=true;FirmwareUpdateState::hardwareReady=true;std::vector<uint8_t> empty;chunk(UPLOAD_FILE_START,empty);}
void finish(){std::vector<uint8_t> empty;chunk(UPLOAD_FILE_END,empty);server.routes["/api/firmware/upload"].end();}
int main(){FirmwareUpdate::attach(server,auth);auto valid=binary();
 reset();prepare(2000000);assert(server.code==400 && !FirmwareUpdateState::requested);
 reset();prepare();chunk(UPLOAD_FILE_START,valid);chunk(UPLOAD_FILE_WRITE,valid);assert(!Update.active); // Workers must quiesce.
 reset();start();std::vector<uint8_t> first(valid.begin(),valid.begin()+17),rest(valid.begin()+17,valid.end());chunk(UPLOAD_FILE_WRITE,first);assert(!Update.active);chunk(UPLOAD_FILE_WRITE,rest);finish();assert(server.code==200 && Update.bytes==valid);testMillis+=3000;FirmwareUpdate::tick();assert(ESP.reboots==1);
 reset();start();auto wrong=valid;wrong[12]=FirmwareUpdateState::ChipId==0?13:0;chunk(UPLOAD_FILE_WRITE,wrong);finish();assert(server.code==400 && !Update.active);
 reset();start();wrong=valid;wrong[32]=0;chunk(UPLOAD_FILE_WRITE,wrong);finish();assert(server.code==400);
 reset();start();chunk(UPLOAD_FILE_WRITE,first);finish();assert(server.code==400 && !FirmwareUpdateState::requested);
 reset();start();wrong=valid;wrong.push_back(0);chunk(UPLOAD_FILE_WRITE,wrong);assert(!FirmwareUpdateState::requested);
 reset();start();Update.failEnd=true;chunk(UPLOAD_FILE_WRITE,valid);finish();assert(server.code==400 && !FirmwareUpdate::success);
 reset();start();Update.failWrite=true;chunk(UPLOAD_FILE_WRITE,valid);assert(!FirmwareUpdateState::requested && Update.aborts);
 reset();start();chunk(UPLOAD_FILE_ABORTED,valid);assert(!FirmwareUpdateState::requested);
 reset();start();server.nonce="wrong";chunk(UPLOAD_FILE_WRITE,valid);assert(!Update.active);finish();assert(server.code==403);
 reset();prepare();FirmwareUpdateState::networkReady=true;FirmwareUpdateState::hardwareReady=true;permitted=false;chunk(UPLOAD_FILE_START,valid);chunk(UPLOAD_FILE_WRITE,valid);assert(!Update.active);
 reset();start();testMillis+=60001;FirmwareUpdate::tick();assert(!FirmwareUpdateState::requested);
 std::cout<<"Firmware streaming, quiescence, header/length/session validation and failure tests passed\n";
}
