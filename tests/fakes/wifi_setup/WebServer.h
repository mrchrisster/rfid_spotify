#pragma once
#include "WiFi.h"
#include <map>
#include <functional>
#include <string>
constexpr int HTTP_GET=0,HTTP_POST=1;
class WebServer {
public:
 std::map<std::pair<std::string,int>,std::function<void()>> handlers;
 IPAddress incoming=IPAddress(192,168,4,1);String body,requestHeader="RFIDPlayer",response;int code=0;
 struct Client {IPAddress address;IPAddress localIP(){return address;}};
 Client client(){return {incoming};}
 String header(const char*){return requestHeader;}String arg(const char*){return body;}
 void sendHeader(const char*,const char*){}
 void send(int c,const char*,const String& s){code=c;response=s;}
 void send_P(int c,const char*,const char* s){code=c;response=s;}
 void on(const char* path,int method,std::function<void()> handler){handlers[{path,method}]=handler;}
 void call(const char* path,int method,const char* data=""){body=data;code=0;handlers.at({path,method})();}
};
