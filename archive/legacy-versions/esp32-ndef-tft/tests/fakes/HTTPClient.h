#pragma once
#include "WiFiClientSecure.h"
#include <assert.h>
#include <deque>
#include <vector>
constexpr int HTTPC_DISABLE_FOLLOW_REDIRECTS=0;
struct FakeResponse {int code;String body;int size=-1;String retryAfter="";int readError=0;};
struct FakeRequest {String method,url,body,authorization,contentLength;};
extern std::deque<FakeResponse> responses;
extern std::vector<FakeRequest> requests;
class HTTPClient {
  FakeResponse response{};FakeRequest request;
  int send(const char* method,const String& body){
    assert(!responses.empty());response=responses.front();responses.pop_front();
    request.method=method;request.body=body;
    // Match the pinned Arduino core: length is automatic only for nonempty bodies.
    if(!body.isEmpty())request.contentLength=String(body.length());
    if((request.method=="PUT"||request.method=="POST")&&body.isEmpty())assert(request.contentLength=="0");
    requests.push_back(request);return response.code;
  }
public:
  void setConnectTimeout(int){}void setTimeout(int){}void setReuse(bool){}void setFollowRedirects(int){}
  bool begin(WiFiClientSecure&,const String& url){request.url=url;return true;}
  void collectHeaders(const char**,size_t){}
  void addHeader(const String& key,const String& value){if(key=="Authorization")request.authorization=value;if(key=="Content-Length")request.contentLength=value;}
  int POST(const String& body){return send("POST",body);}int PUT(const String& body){return send("PUT",body);}int GET(){return send("GET","");}
  String header(const char*){return response.retryAfter;}
  int getSize(){return response.size;}
  int writeToStream(Stream* stream){
    size_t length=response.body.length(),pos=0;
    while(pos<length){size_t chunk=std::min(size_t(13),length-pos);
      if(stream->write(reinterpret_cast<const uint8_t*>(response.body.c_str())+pos,chunk)!=chunk)return -1;
      pos+=chunk;
    }
    return response.readError?response.readError:int(length);
  }
  void end(){}
};
