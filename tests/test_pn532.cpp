#include "Pn532.h"
#include "RfidPresence.h"
#include <vector>
#include <deque>
#include <array>
#include <cassert>
#include <cstdio>
struct Bus {
  uint32_t time=0;bool stuck=false,broken=false;int corrupt=0;
  std::deque<std::vector<uint8_t>> frames;
  std::vector<uint8_t> scan={0},dataReply={0};
  std::vector<uint8_t> last;
  uint32_t now(){return time;}void wait(unsigned n){time+=n;}void reset(){frames.clear();}
  bool ready(){return !stuck&&!frames.empty();}
  bool write(const uint8_t* p,size_t n){
    last.assign(p,p+n);if(broken)return false;
    assert(n==size_t(p[3])+7 && uint8_t(p[3]+p[4])==0);
    uint8_t sum=0;for(size_t i=5;i<n-1;++i)sum+=p[i];assert(sum==0);
    frames.push_back({0,0,0xff,0,0xff,0});
    std::vector<uint8_t> payload;
    switch(p[6]){
      case 2:payload={0x32,1,6,7};break;
      case 0x14:assert(p[7]==1 && p[9]==1);break;
      case 0x32:assert(p[7]==5 && p[10]==0);break;
      case 0x4a:payload=scan;break;
      case 0x40:payload=dataReply;break;
      case 0x52:payload={0};break;
      default:assert(false);
    }
    std::vector<uint8_t> response(64);
    response[2]=0xff;response[3]=payload.size()+2;response[4]=uint8_t(0-response[3]);
    response[5]=0xd5;response[6]=p[6]+1;
    sum=response[5]+response[6];for(size_t i=0;i<payload.size();++i){response[7+i]=payload[i];sum+=payload[i];}
    response[7+payload.size()]=uint8_t(0-sum);
    if(corrupt)response[corrupt]^=1;
    frames.push_back(response);return true;
  }
  bool read(uint8_t* out,size_t n){
    if(frames.empty()||broken)return false;auto f=frames.front();frames.pop_front();
    assert(f.size()==n);memcpy(out,f.data(),n);return true;
  }
};
struct MemoryChip {
  struct {uint8_t sak=0,size=7,uidByte[10]={1,2,3,4,5,6,7};}uid;
  std::vector<uint8_t> memory;
  bool broken=false;unsigned auths=0;
  bool data(const uint8_t* cmd,size_t count,uint8_t* out,size_t expected){
    if(broken)return false;
    if(cmd[0]==0x60){assert(count==12 && expected==0);assert(!memcmp(cmd+8,uid.uidByte+3,4));++auths;return true;}
    assert(cmd[0]==0x30 && count==2 && expected==16);
    size_t at=cmd[1]*(uid.sak==8?16:4);if(at+16>memory.size())return false;
    memcpy(out,memory.data()+at,16);return true;
  }
};
int main(){
  using Chip=Pn532<Bus>;using Poll=Chip::Poll;
  Bus b;Chip chip(b);assert(chip.begin());assert(chip.version==0x32010607);
  assert(chip.poll()==Poll::Pending);assert(chip.poll()==Poll::Absent);
  b.scan={1,1,0,0x44,0,7,1,2,3,4,5,6,7};
  assert(chip.poll()==Poll::Pending);assert(chip.poll()==Poll::Card);assert(chip.uid.size==7 && chip.uid.sak==0);
  RfidPresence presence;assert(presence.seen(chip.uid.uidByte,chip.uid.size));assert(chip.release());
  for(int i=0;i<30;++i){assert(chip.poll()==Poll::Pending);assert(chip.poll()==Poll::Card);assert(!presence.seen(chip.uid.uidByte,chip.uid.size));assert(chip.release());}
  // Invalid framing never becomes absence; latch survives fault and recovery.
  for(int offset: {2,3,4,5,6,8}){
    b.scan={0};b.corrupt=offset;assert(chip.poll()==Poll::Pending);assert(chip.poll()==Poll::Error);
    presence.uncertain();b.corrupt=0;assert(chip.begin());assert(!presence.seen(chip.uid.uidByte,chip.uid.size));
  }
  for(unsigned size: {0,3,8,11,255}){
    b.scan={1,1,0,0,0,uint8_t(size),1,2,3,4,5,6,7};
    assert(chip.poll()==Poll::Pending);assert(chip.poll()==Poll::Error);
  }
  b.scan={1,1,0,0,0,10,1,2,3};assert(chip.poll()==Poll::Pending);assert(chip.poll()==Poll::Error);
  b.scan={0,1};assert(chip.poll()==Poll::Pending);assert(chip.poll()==Poll::Error);
  b.scan={0};b.time=UINT32_MAX-100;assert(chip.poll()==Poll::Pending);b.stuck=true;
  b.time+=499;assert(chip.poll()==Poll::Pending);++b.time;assert(chip.poll()==Poll::Error);
  presence.uncertain();assert(!presence.missing(100,false));b.stuck=false;assert(chip.begin());
  assert(!presence.missing(1000,true));assert(presence.missing(2500,true));
  uint8_t cmd[]={0x30,4},out[16];b.dataReply.assign(17,0);assert(chip.data(cmd,2,out,16));
  b.dataReply[0]=1;assert(!chip.data(cmd,2,out,16));b.dataReply={0};assert(!chip.data(cmd,2,out,16));
  b.broken=true;assert(!chip.begin());b.broken=false;b.stuck=true;assert(!chip.begin());assert(b.time>=100);
  for(size_t cap=16;cap<=1008;cap+=8){
    MemoryChip m;m.memory.resize(cap+16);m.memory[12]=0xe1;m.memory[13]=0x10;m.memory[14]=cap/8;
    for(size_t i=0;i<cap;++i)m.memory[i+16]=i%251;
    Pn532Ndef<MemoryChip> tag(m);assert(tag.begin());std::vector<uint8_t> actual(cap);assert(tag.read(0,actual.data(),cap));
    for(size_t i=0;i<cap;++i)assert(actual[i]==i%251);
    assert(!tag.read(cap,actual.data(),1));
  }
  MemoryChip m;m.uid.sak=8;m.memory.resize(1024);
  for(size_t i=0;i<720;++i){size_t block=4+i/48*4+i%48/16;m.memory[block*16+i%16]=i%251;}
  Pn532Ndef<MemoryChip> classic(m);assert(classic.begin());uint8_t all[720];assert(classic.read(0,all,720));
  for(size_t i=0;i<720;++i)assert(all[i]==i%251);assert(m.auths==45);
  m.broken=true;Pn532Ndef<MemoryChip> failed(m);assert(failed.begin());assert(!failed.read(0,all,1)&&failed.ioError);
  m.uid.sak=0x20;Pn532Ndef<MemoryChip> unsupported(m);assert(!unsupported.begin());
  // Real NDEF URI record enters the same SafeNdef parser as MFRC522.
  MemoryChip tagChip;tagChip.memory.resize(160);tagChip.memory[12]=0xe1;tagChip.memory[13]=0x10;tagChip.memory[14]=18;
  const char* uri="spotify:album:1234567890123456789012";size_t length=strlen(uri);
  uint8_t* bytes=tagChip.memory.data()+16;bytes[0]=3;bytes[1]=length+5;bytes[2]=0xd1;bytes[3]=1;bytes[4]=length+1;bytes[5]='U';bytes[6]=0;
  memcpy(bytes+7,uri,length);bytes[7+length]=0xfe;
  char parsed[SafeNdef::MaxUri]={};Pn532Ndef<MemoryChip> realTag(tagChip);assert(realTag.spotifyUri(parsed,sizeof(parsed)));assert(!strcmp(parsed,uri));
  tagChip.memory[16+4]=250;Pn532Ndef<MemoryChip> malformed(tagChip);assert(!malformed.spotifyUri(parsed,sizeof(parsed)));
  puts("PN532 frame, timeout, presence, Type2 and Classic tests passed");
}
