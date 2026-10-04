#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "HardwareProfile.h"
class Pn532Wire {
  TwoWire wire{0};
  bool initialized=false;
public:
  uint32_t now(){return millis();}
  void wait(unsigned ms){delay(ms);}
  bool ready(){return digitalRead(PLAYER_PN532_IRQ)==LOW;}
  void reset(){
    pinMode(PLAYER_PN532_IRQ,INPUT_PULLUP);
    pinMode(PLAYER_PN532_RESET,OUTPUT);
    digitalWrite(PLAYER_PN532_RESET,LOW);delay(20);
    digitalWrite(PLAYER_PN532_RESET,HIGH);delay(100);
    if(initialized)wire.end();
    initialized=wire.begin(PLAYER_PN532_SDA,PLAYER_PN532_SCL,100000);
    wire.setTimeOut(25);
  }
  bool write(const uint8_t* data,size_t size){
    if(!initialized)return false;
    wire.beginTransmission(0x24);
    if(wire.write(data,size)!=size){wire.endTransmission();return false;}
    return wire.endTransmission()==0;
  }
  bool read(uint8_t* out,size_t size){
    if(!initialized || wire.requestFrom(uint8_t(0x24),size+1)!=size+1)return false;
    if(wire.read()!=1){while(wire.available())wire.read();return false;}
    for(size_t i=0;i<size;++i)out[i]=uint8_t(wire.read());
    return true;
  }
};
