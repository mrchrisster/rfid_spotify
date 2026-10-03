#include <Arduino.h>
#include <HTTPClient.h>
using SafeTlsClient=WiFiClientSecure;
void logMessage(const String&){}
#define ARDUINO_ARCH_ESP32
#include "ArtworkSpool.h"
#include <cassert>
uint32_t testMillis=0;
std::deque<FakeResponse> responses;
std::vector<FakeRequest> requests;
int main(){
 SafeTlsClient client;uint8_t* image=nullptr;int error=0;
 fsFiles["/unrelated"]="keep";
 auto run=[&](FakeResponse response,size_t limit){HTTPClient http;http.begin(client,"https://i.scdn.co/image/x");responses.push_back(response);http.GET();return ArtworkSpool::download(http,client,image,limit,error);};
 std::string jpeg(55647,'J');
 assert(run({200,jpeg.c_str(),55647},65536)==55647);assert(!image && ArtworkSpool::busy.load());
 assert(fsFiles[ArtworkSpool::Path]==jpeg && fsEnds==0);
 // The queued file cannot be overwritten until the hardware consumer releases it.
 assert(!run({200,"other",5},65536) && error==429 && fsFiles[ArtworkSpool::Path]==jpeg);
 ArtworkSpool::release();assert(!ArtworkSpool::busy.load());
 assert(fsFiles.size()==1 && fsFiles["/unrelated"]=="keep" && fsEnds==1);
 assert(!run({200,"short",20},65536) && !image && !ArtworkSpool::busy.load());
 fsWriteOK=false;assert(!run({200,"JPEG",4},65536) && !image);fsWriteOK=true;
 assert(!run({200,"oversize",8},4) && !image);
 fsMountOK=false;partitionBlank=false;
 assert(!run({200,"JPEG",4},65536) && fsFormats==0 && fsFiles["/unrelated"]=="keep");
 partitionBlank=true;assert(run({200,"JPEG",4},65536)==4 && fsFormats==1 && !image);
 ArtworkSpool::release();assert(fsFiles.count(ArtworkSpool::Path)==0);
}
