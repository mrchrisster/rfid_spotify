#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "SafeNdef.h"

// Small bounded PN532 host protocol (NXP UM0701-02). Transport is injected for
// tests. No third-party NDEF parser or UID-length-based tag guessing.
// I2C status/IRQ are handled by transport; frames here exclude the status byte.
template<class Transport> class Pn532 {
  Transport& bus;
  bool pending = false;
  uint32_t started = 0;
  uint8_t buffer[64] = {};
  bool waitReady(uint32_t timeout) {
    uint32_t at=bus.now();
    while (!bus.ready()) {
      if(uint32_t(bus.now()-at)>=timeout)return false;
      bus.wait(1);
    }
    return true;
  }
  bool start(uint8_t command, const uint8_t* data, size_t n) {
    if(n>48)return false;
    uint8_t frame[57]={0,0,0xff,uint8_t(n+2),uint8_t(0-(n+2)),0xd4,command};
    uint8_t sum=uint8_t(0xd4+command);
    for(size_t i=0;i<n;++i){frame[7+i]=data[i];sum+=data[i];}
    frame[7+n]=uint8_t(0-sum);frame[8+n]=0;
    uint8_t ack[6];
    static const uint8_t expected[]={0,0,0xff,0,0xff,0};
    return bus.write(frame,n+9) && waitReady(100) && bus.read(ack,6) && !memcmp(ack,expected,6);
  }
  bool response(uint8_t command, uint8_t* out, size_t& n) {
    // One bounded I2C read consumes the frame; never split it into transactions.
    if(!bus.read(buffer,sizeof(buffer)))return false;
    size_t len=buffer[3];
    if(buffer[0] || buffer[1] || buffer[2]!=0xff || len<2 || len>sizeof(buffer)-7 ||
       uint8_t(len+buffer[4]) || buffer[5]!=0xd5 || buffer[6]!=uint8_t(command+1) ||
       buffer[len+6] || len-2>n)return false;
    uint8_t sum=0;for(size_t i=5;i<6+len;++i)sum+=buffer[i];
    if(sum)return false;
    n=len-2;memcpy(out,buffer+7,n);return true;
  }
  bool exchange(uint8_t command,const uint8_t* data,size_t count,uint8_t* out,size_t& n) {
    return start(command,data,count) && waitReady(200) && response(command,out,n);
  }
public:
  enum class Poll { Pending, Absent, Card, Error };
  struct Uid {uint8_t uidByte[10]={},size=0,sak=0;} uid;
  uint32_t version=0;
  explicit Pn532(Transport& t):bus(t){}
  bool begin() {
    pending=false;version=0;bus.reset();
    uint8_t out[8];size_t n=sizeof(out);
    if(!exchange(0x02,nullptr,0,out,n)||n!=4||out[0]!=0x32)return false;
    version=(uint32_t(out[0])<<24)|(uint32_t(out[1])<<16)|(uint32_t(out[2])<<8)|out[3];
    uint8_t sam[]={1,0x14,1};n=sizeof(out);
    if(!exchange(0x14,sam,sizeof(sam),out,n)||n!=0)return false;
    // Finite RF attempts: only an explicit NbTg=0 response counts as absence.
    uint8_t retries[]={5,0xff,1,0};n=sizeof(out);
    return exchange(0x32,retries,sizeof(retries),out,n)&&n==0;
  }
  Poll poll() {
    if(!pending){
      uint8_t data[]={1,0};
      if(!start(0x4a,data,sizeof(data)))return Poll::Error;
      pending=true;started=bus.now();return Poll::Pending;
    }
    if(!bus.ready()){
      if(uint32_t(bus.now()-started)<500)return Poll::Pending;
      pending=false;return Poll::Error;
    }
    pending=false;uint8_t out[48];size_t n=sizeof(out);
    if(!response(0x4a,out,n)||!n)return Poll::Error;
    if(out[0]==0)return n==1?Poll::Absent:Poll::Error;
    if(n<6 || out[0]!=1 || out[1]!=1)return Poll::Error;
    uint8_t size=out[5];
    if((size!=4 && size!=7 && size!=10)||n<size_t(6+size))return Poll::Error;
    uid.size=size;uid.sak=out[4];memcpy(uid.uidByte,out+6,size);return Poll::Card;
  }
  bool data(const uint8_t* request,size_t count,uint8_t* out,size_t expected) {
    if(count>46 || expected>32)return false;
    uint8_t command[48]={1};memcpy(command+1,request,count);
    uint8_t result[33];size_t n=sizeof(result);
    if(!exchange(0x40,command,count+1,result,n)||n!=expected+1||result[0]!=0)return false;
    memcpy(out,result+1,expected);return true;
  }
  bool release() {
    uint8_t target=1,out[2];size_t n=sizeof(out);
    return exchange(0x52,&target,1,out,n)&&n==1&&out[0]==0;
  }
};

template<class Chip> class Pn532Ndef {
  Chip& chip;
  bool classic=false;
  size_t capacity=0,cached=size_t(-1);
  uint8_t cache[16]={};
  bool fail(){ioError=true;return false;}
  bool block(uint8_t address,uint8_t* out){uint8_t cmd[]={0x30,address};return chip.data(cmd,2,out,16)||fail();}
public:
  bool ioError=false;
  explicit Pn532Ndef(Chip& c):chip(c){}
  bool begin(){
    classic=chip.uid.sak==0x08;
    if(classic){capacity=720;return true;}
    if(chip.uid.sak!=0)return false;
    uint8_t cc[16];if(!block(3,cc))return false;
    if(cc[0]!=0xe1 || (cc[1]>>4)!=1 || (cc[3]>>4)!=0)return false;
    capacity=size_t(cc[2])*8;return capacity>=16 && capacity<=1008;
  }
  bool read(size_t offset,uint8_t* out,size_t size){
    if(offset>capacity||size>capacity-offset)return false;
    while(size){
      size_t base=offset&~size_t(15);
      if(!classic && base+16>capacity)base=capacity-16;
      if(base!=cached){
        uint8_t address;
        if(classic){
          address=4+(base/48)*4+(base%48)/16;
          uint8_t auth[]={0x60,address,0xd3,0xf7,0xd3,0xf7,0xd3,0xf7,0,0,0,0};
          if(chip.uid.size<4)return false;
          memcpy(auth+8,chip.uid.uidByte+chip.uid.size-4,4);
          uint8_t ignored=0;if(!chip.data(auth,sizeof(auth),&ignored,0))return fail();
        }else address=4+base/4;
        if(!block(address,cache))return false;cached=base;
      }
      size_t available=16-(offset-base),count=size<available?size:available;
      memcpy(out,cache+(offset-base),count);offset+=count;out+=count;size-=count;
    }
    return true;
  }
  bool spotifyUri(char* uri,size_t n){return begin()&&SafeNdef::read(*this,capacity,uri,n);}
};
