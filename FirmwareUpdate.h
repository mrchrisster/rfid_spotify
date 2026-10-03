#pragma once
#include <Update.h>
#include <esp_ota_ops.h>
#include "FirmwareUpdateState.h"
// Main-loop owned streaming uploader. Workers acknowledge a safe boundary;
// no task suspension while it might own SPI/NVS/TLS state.
namespace FirmwareUpdate {
inline size_t expected=0,received=0,headerSize=0;
inline uint8_t header[36];
inline bool writing=false,uploaded=false,success=false,acceptUpload=false;
inline uint32_t activity=0,rebootAt=0;
inline String session,message;
inline void fail(const char* why){
 if(writing)Update.abort();writing=false;success=false;message=why;
 FirmwareUpdateState::requested=false;
}
inline void tick(){
 if(success && uint32_t(millis()-rebootAt)>=3000){ESP.restart();return;}
 if(FirmwareUpdateState::requested && uint32_t(millis()-activity)>60000)fail("Update timed out; current firmware retained");
}
inline void attach(WebServer& server,bool(*authorize)(bool)){
 server.on("/api/firmware/prepare",HTTP_POST,[&server,authorize](){
  if(!authorize(true))return;
  if(FirmwareUpdateState::requested || FirmwareUpdateState::networkReady || FirmwareUpdateState::hardwareReady || success){server.send(409,"application/json","{\"message\":\"An update is already active; retry shortly\"}");return;}
  JsonDocument doc;const esp_partition_t* target=esp_ota_get_next_update_partition(nullptr);
  if(deserializeJson(doc,server.arg("plain")) || !doc["size"].is<uint32_t>() || !target || doc["size"].as<uint32_t>()<36 || doc["size"].as<uint32_t>()>target->size){server.send(400,"application/json","{\"message\":\"Invalid file size or no suitable OTA partition\"}");return;}
  expected=doc["size"];received=headerSize=0;writing=uploaded=success=false;message="Preparing player";
  char nonce[33];snprintf(nonce,sizeof(nonce),"%08lx%08lx%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());session=nonce;
  activity=millis();FirmwareUpdateState::percent=0;FirmwareUpdateState::requested=true;
  doc.clear();doc["session"]=session;String reply;serializeJson(doc,reply);server.send(202,"application/json",reply);
 });
 server.on("/api/firmware/status",HTTP_GET,[&server,authorize](){
  if(!authorize(false))return;
  JsonDocument doc;doc["ready"]=FirmwareUpdateState::ready();doc["active"]=FirmwareUpdateState::requested.load();doc["message"]=message;doc["percent"]=FirmwareUpdateState::percent.load();String reply;serializeJson(doc,reply);server.send(200,"application/json",reply);
 });
 server.on("/api/firmware/upload",HTTP_POST,[&server,authorize](){
  if(!authorize(true))return;
  if(server.header("X-Firmware-Session")!=session || session.isEmpty()){server.send(403,"application/json","{\"message\":\"Invalid update session\"}");return;}
  if(!uploaded || !writing || received!=expected){if(FirmwareUpdateState::requested)fail("Incomplete upload; current firmware retained");}
  else if(!Update.end(false))fail("Firmware validation failed; current firmware retained");
  else {writing=false;success=true;rebootAt=millis();message="Firmware installed. Restarting in 3 seconds.";FirmwareUpdateState::percent=100;}
  JsonDocument doc;doc["message"]=message;String reply;serializeJson(doc,reply);server.send(success?200:400,"application/json",reply);
 },[&server,authorize](){
  HTTPUpload& upload=server.upload();
  if(upload.status==UPLOAD_FILE_START){
   acceptUpload=false;
   if(!authorize(true))return;
   if(!FirmwareUpdateState::ready() || server.header("X-Firmware-Session")!=session || session.isEmpty())return;
   if(writing || uploaded){fail("Only one application binary per update is allowed");return;}
   acceptUpload=true;activity=millis();
  }else if(upload.status==UPLOAD_FILE_WRITE){
   if(!acceptUpload)return;
   if(!FirmwareUpdateState::ready() || server.header("X-Firmware-Session")!=session)return;
   activity=millis();
   if(upload.currentSize>expected-received){fail("File exceeds declared size");return;}
   size_t offset=0;
   while(headerSize<sizeof(header) && offset<upload.currentSize)header[headerSize++]=upload.buf[offset++];
   if(headerSize==sizeof(header) && !writing){
    if(!FirmwareUpdateState::applicationHeader(header,sizeof(header))){fail("Use the ESP32 application .bin, not a merged image or bootloader");return;}
    if(!Update.begin(expected,U_FLASH)){fail("Cannot open inactive firmware slot");return;}
    writing=true;
    if(Update.write(header,sizeof(header))!=sizeof(header)){fail("Firmware write failed");return;}
   }
   if(writing && offset<upload.currentSize && Update.write(upload.buf+offset,upload.currentSize-offset)!=upload.currentSize-offset){fail("Firmware write failed");return;}
   received+=upload.currentSize;FirmwareUpdateState::percent=unsigned(received*100/expected);message="Uploading firmware";
  }else if(upload.status==UPLOAD_FILE_END){if(acceptUpload && server.header("X-Firmware-Session")==session && FirmwareUpdateState::requested)uploaded=true;}
  else if(upload.status==UPLOAD_FILE_ABORTED){if(server.header("X-Firmware-Session")==session)fail("Upload interrupted; current firmware retained");}
 });
}
}
