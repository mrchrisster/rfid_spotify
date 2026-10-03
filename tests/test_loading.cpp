#include "LoadingState.h"
#include <cassert>
int main(){
 LoadingState s;s.begin(100);s.job=7;assert(s.active && s.frameDue(100));
 s.frameAt=100;assert(!s.frameDue(219)&&s.frameDue(220));
 assert(!s.accepts(6)&&s.accepts(7)&&s.accepts(8));
 s.fail(6,300);assert(!s.failed);s.fail(7,300);assert(s.failed && !s.frameDue(500));
 assert(!s.expired(5299)&&s.expired(5300));
 s.fail(7,300,1);assert(s.error==1);
 auto generation=s.generation;s.begin(4000);assert(s.generation!=generation && !s.failed && !s.drawn);
 assert(!s.expired(93999)&&s.expired(94000));s.stop();assert(!s.active);
 s.begin(UINT32_MAX-500);assert(!s.expired(89498)&&s.expired(89499));
 s.begin(100);s.job=10;s.fail(0,100,1);assert(!s.accepts(9));
 s.fail(9,100,3);assert(s.error==1);
 s.job=UINT32_MAX;assert(s.accepts(1)&&!s.accepts(UINT32_MAX-1));
}
