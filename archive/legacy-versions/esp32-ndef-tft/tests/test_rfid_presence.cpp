#include "RfidPresence.h"
#include "RfidRecovery.h"
#include <assert.h>
#include <stdio.h>
#include <vector>
struct Pins {
  bool outputMode=false, high=false;
  std::vector<int> events;
  void output(uint8_t pin) { assert(pin==4); outputMode=true;events.push_back(1); }
  void write(uint8_t pin,bool level) { assert(pin==4 && outputMode);high=level;events.push_back(level?3:2); }
  void wait(unsigned duration) { assert(duration==50);events.push_back(4); }
};
struct Reader {
  static constexpr uint8_t UNUSED_PIN=255;
  Pins& pins; unsigned resets=0;
  void PCD_Init(uint8_t select,uint8_t reset) {
    assert(select==5 && reset==UNUSED_PIN);assert(pins.outputMode && pins.high);++resets;
  }
};
int main() {
  uint8_t a[]={1,2,3,4},b[]={5,6,7,8};
  RfidPresence presence;
  assert(presence.seen(a,4));
  // Reproduce the old three-timeout pattern: a held card must not enqueue again.
  for(unsigned cycle=0;cycle<20;++cycle){
    for(unsigned j=1;j<=3;++j)assert(!presence.missing(cycle*600+j*100,true));
    assert(!presence.seen(a,4));
  }
  assert(!presence.missing(20000,true));
  assert(!presence.missing(21000,true));
  presence.uncertain(); // Reader reset / selection error must not count as removal.
  assert(!presence.missing(30000,false));
  assert(!presence.seen(a,4));
  assert(presence.seen(b,4)); // A different UID need not wait through removal delay.
  assert(!presence.missing(40000,true));
  assert(!presence.missing(41499,true));
  assert(presence.missing(41500,true));
  assert(presence.seen(b,4));
  assert(!presence.seen(b,11)); // No unbounded UID copy.
  assert(!presence.missing(UINT32_MAX-500,true));
  assert(presence.missing(1000,true)); // millis rollover.
  assert(presence.seen(a,4));
  RfidRecovery::Cooldown gate;
  assert(gate.acquire(0));
  assert(!gate.acquire(150)); // Failed reset immediately followed by another trigger.
  assert(!gate.acquire(4999));
  assert(gate.acquire(5000));
  assert(!gate.acquire(5001));
  RfidRecovery::Cooldown rollover;
  assert(rollover.acquire(UINT32_MAX-1000));
  assert(!rollover.acquire(3998));
  assert(rollover.acquire(3999));
  Pins pins;Reader reader{pins};
  RfidRecovery::reset(reader,pins,5,4);
  assert((pins.events==std::vector<int>{1,2,4,3,4}));
  pins.outputMode=false; // Reproduce the prior library leaving the pin as input.
  RfidRecovery::reset(reader,pins,5,4);
  assert(reader.resets==2 && pins.outputMode && pins.high);
  RfidRecovery::Registers good{0x92,3,0x80,0xa9,0};assert(good.healthy());
  auto bad=good;bad.version=0xff;assert(!bad.healthy());
  bad=good;bad.version=0;assert(!bad.healthy());
  bad=good;bad.antenna=0;assert(!bad.healthy());
  bad=good;bad.timer=0;assert(!bad.healthy());
  bad=good;bad.prescaler=0;assert(!bad.healthy());
  puts("Held-card suppression, reader-fault latch and reset-pin ownership tests passed");
}
