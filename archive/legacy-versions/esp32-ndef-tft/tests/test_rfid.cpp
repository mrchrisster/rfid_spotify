#include "RfidReader.h"
#include <assert.h>
#include <stdio.h>
int main(){
  // Every advertised Type-2 capacity, especially the last partial 16-byte group.
  for(size_t capacity=16;capacity<=1008;capacity+=8){
    MFRC522 chip;chip.memory.resize(capacity+16);
    chip.memory[12]=0xe1;chip.memory[13]=0x10;chip.memory[14]=capacity/8;chip.memory[15]=0;
    for(size_t i=0;i<capacity;++i)chip.memory[16+i]=i%251;
    RfidReader reader(chip);assert(reader.begin());
    std::vector<uint8_t> actual(capacity);assert(reader.read(0,actual.data(),actual.size()));
    for(size_t i=0;i<capacity;++i)assert(actual[i]==i%251);
    assert(!reader.read(capacity,actual.data(),1));
    for(size_t i=0;i<capacity;++i){uint8_t byte;assert(reader.read(i,&byte,1));assert(byte==i%251);}
  }
  MFRC522 classic;classic.type=MFRC522::PICC_TYPE_MIFARE_1K;classic.memory.resize(1024,0xee);
  for(size_t i=0;i<720;++i){size_t block=4+(i/48)*4+(i%48)/16;classic.memory[block*16+i%16]=i%251;}
  RfidReader reader(classic);assert(reader.begin());uint8_t actual[720];assert(reader.read(0,actual,sizeof(actual)));
  for(size_t i=0;i<720;++i)assert(actual[i]==i%251);
  assert(classic.auths==45);classic.fail=true;RfidReader broken(classic);assert(broken.begin());assert(!broken.read(0,actual,16)&&broken.ioError);
  assert(broken.lastStatus==MFRC522::STATUS_ERROR && broken.errorAddress==4);
  assert(strcmp(broken.errorOperation,"authenticate")==0);
  MFRC522 absent;absent.fail=true;RfidReader missing(absent);assert(!missing.begin());
  assert(missing.ioError && missing.lastStatus==MFRC522::STATUS_ERROR && missing.errorAddress==3);
  assert(strcmp(missing.errorOperation,"capability read")==0);
  MFRC522 small;small.memory.resize(32);small.memory[12]=0xe1;small.memory[13]=0x10;small.memory[14]=255;
  RfidReader invalid(small);assert(!invalid.begin());
  puts("RFID Type-2 capacity/boundary tests and Classic sector mapping passed");
}
