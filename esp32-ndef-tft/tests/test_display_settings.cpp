#include "DisplaySettings.h"
#include "CoverLayout.h"
#include "StartupPolicy.h"
#include <assert.h>
#include <stdio.h>
#include <string>
int main(){
 using namespace DisplaySettings;
 assert(!StartupPolicy::authDue(2000,0,true,false,false,0));
 assert(StartupPolicy::authDue(2000,0,true,true,false,0));
 assert(!StartupPolicy::authDue(2500,2000,true,true,false,0));
 assert(StartupPolicy::authDue(3000,2000,true,true,false,0));
 assert(!StartupPolicy::authDue(3000,2000,true,true,false,5000));
 assert(!StartupPolicy::authDue(3000,2000,true,true,true,0));
 assert(StartupPolicy::authDue(32000,2000,true,true,true,0));
 assert(StartupPolicy::authDue(500,UINT32_MAX-1000,true,true,false,0));
 assert(StartupPolicy::waitForAuth(false,true,true,false,200));
 assert(StartupPolicy::waitForAuth(true,false,false,false,-2));
 assert(StartupPolicy::waitForAuth(true,true,false,false,-1));
 assert(!StartupPolicy::waitForAuth(true,true,true,false,200));
 assert(!StartupPolicy::waitForAuth(true,true,false,true,401));
 assert(!StartupPolicy::waitForAuth(true,true,false,false,400));
 for(int size:{64,300,640}) {
  CoverLayout::Layout l(size,size);assert(l.width==320 && l.height==320 && l.x==0 && l.y==-40);
  for(int y=0;y<240;++y)for(int x=0;x<320;++x) {
    int sx=l.sourceX(x),sy=l.sourceY(y);assert(sx>=0 && sx<size && sy>=0 && sy<size);
    assert(l.left(sx)<=x && l.left(sx+1)>x);assert(l.top(sy)<=y && l.top(sy+1)>y);
  }
 }
 for(auto dimensions:{std::pair<int,int>{640,360},{240,640},{301,299}}) {
  CoverLayout::Layout l(dimensions.first,dimensions.second);
  assert(l.width>=320 && l.height>=240);
  for(int x=0;x<320;++x){int sx=l.sourceX(x);assert(l.left(sx)<=x && l.left(sx+1)>x);}
 }

 assert(validSeconds(0)&&validSeconds(10)&&validSeconds(86400));
 assert(!validSeconds(1)&&!validSeconds(9)&&!validSeconds(86401)&&!validSeconds(UINT32_MAX));
 auto packed=pack(true,86400);assert(debug(packed)&&seconds(packed)==86400);
 assert(!debug(pack(false,600))&&seconds(pack(false,600))==600);
 assert(!expired(100000,1,0));assert(!expired(10999,1000,10));assert(expired(11000,1000,10));
 assert(!expired(8998,UINT32_MAX-1000,10));assert(expired(8999,UINT32_MAX-1000,10));
 LogSnapshot log;log.append(nullptr);log.append("");assert(log.revision==0);
 log.append("hello");assert(strcmp(log.lines[17],"hello")==0);
 std::string longLine(223,'x');log.append(longLine.c_str());assert(log.revision==6);
 assert(strlen(log.lines[17])==15 && strlen(log.lines[16])==52);
 log.append("one\ntwo");assert(strcmp(log.lines[16],"one")==0&&strcmp(log.lines[17],"two")==0);
 for(unsigned i=0;i<100;++i) log.append(std::to_string(i).c_str());
 assert(strcmp(log.lines[0],"82")==0&&strcmp(log.lines[17],"99")==0);
 log.append("\x01\xff");assert(strcmp(log.lines[17],"??")==0);
 puts("Display settings bounds, timeout rollover, log wrapping and bounded history passed");
}
