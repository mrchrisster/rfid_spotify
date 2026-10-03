#pragma once
#include "Arduino.h"
#include <string>
#include <map>
#define FILE_WRITE "w"
#define FILE_READ "r"
inline bool fsMountOK=true,fsWriteOK=true,fsReadOK=true;
inline int fsFormats=0,fsEnds=0;
inline std::map<std::string,std::string> fsFiles;
class File {
 std::string path;bool open=false;
public:
 File()=default;
 File(const char* p,const char* mode):path(p),open(true){if(*mode=='w')fsFiles[path]="";}
 operator bool() const{return open;}
 size_t write(const uint8_t* data,size_t size){if(!fsWriteOK)return 0;fsFiles[path].append(reinterpret_cast<const char*>(data),size);return size;}
 size_t read(uint8_t* data,size_t size){if(!fsReadOK)return 0;size=std::min(size,fsFiles[path].size());memcpy(data,fsFiles[path].data(),size);return size;}
 void close(){open=false;}void flush(){}
};
struct FakeFS {
 bool begin(bool format){if(format)++fsFormats;return fsMountOK||format;}
 bool remove(const char* p){return fsFiles.erase(p);}
 File open(const char* p,const char* mode){return File(p,mode);}
 void end(){++fsEnds;}
};
inline FakeFS SPIFFS;
