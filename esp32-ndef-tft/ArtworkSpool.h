#pragma once
#if defined(ARDUINO_ARCH_ESP32)
#include <SPIFFS.h>
#include <esp_partition.h>
#include <atomic>

namespace ArtworkSpool {
inline std::atomic<bool> busy{false};
inline bool mounted=false;
constexpr const char* Path="/.rfid-cover-download.jpg";
// Never format an existing nonempty partition on a mount failure.
inline bool mount() {
  if(mounted)return true;
  if(SPIFFS.begin(false)){mounted=true;return true;}
  const esp_partition_t* partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,nullptr);
  if(!partition)return false;
  uint8_t block[256];
  for(size_t offset=0;offset<partition->size;offset+=sizeof(block)) {
    if(esp_partition_read(partition,offset,block,sizeof(block))!=ESP_OK)return false;
    for(uint8_t byte:block)if(byte!=0xff)return false;
    taskYIELD();
  }
  mounted=SPIFFS.begin(true);return mounted;
}
class Sink:public Stream {
  File& file;size_t limit;uint32_t started=millis();
public:
  size_t count=0;bool failed=false;
  Sink(File& file,size_t limit):file(file),limit(limit){}
  int available() override{return 0;}
  int read() override{return -1;}
  int peek() override{return -1;}
  void flush() override{file.flush();}
  size_t write(uint8_t b) override{return write(&b,1);}
  size_t write(const uint8_t* bytes,size_t size) override {
    if(failed || size>limit-count || uint32_t(millis()-started)>10000){failed=true;return 0;}
    size_t written=file.write(bytes,size);count+=written;
    if(written!=size)failed=true;
    return written;
  }
};
// Ownership transfers network -> result queue -> hardware worker on success.
inline void release() {
  SPIFFS.remove(Path);SPIFFS.end();mounted=false;busy.store(false);
}
inline int download(HTTPClient& http,SafeTlsClient& client,uint8_t*& buffer,size_t limit,int& error) {
  (void)buffer; // File-backed success intentionally has no full-image RAM buffer.
  if(busy.exchange(true)){http.end();client.stop();error=429;return 0;}
  if(!mount()) {
    logMessage("[Art] Flash mount failed; existing nonempty partition will not be formatted");
    http.end();client.stop();busy.store(false);error=507;return 0;
  }
  SPIFFS.remove(Path);
  File file=SPIFFS.open(Path,FILE_WRITE);
  size_t count=0;bool complete=false;int result=-1;int expected=http.getSize();
  if(file) {
    Sink sink(file,limit);result=http.writeToStream(&sink);count=sink.count;
    complete=result>=0 && !sink.failed && count>0 && (expected<0 || count==size_t(expected));
    file.close();
  }else logMessage("[Art] Cannot open temporary JPEG for writing");
  http.end();client.stop();
  if(!complete){
    logMessage("[Art] Flash download incomplete: written="+String(count)+" expected="+String(expected)+" transport="+String(result));
    release();error=502;return 0;
  }
  logMessage("[Art] JPEG saved to flash; decoding directly without a full-image RAM allocation");
  error=200;return int(count); // File remains owned until the display consumes it.
}
}
#endif
