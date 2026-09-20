#pragma once
#include <stdint.h>
#include <string.h>
#include <vector>
class MFRC522 {
public:
  enum PICC_Type { PICC_TYPE_MIFARE_1K, PICC_TYPE_MIFARE_UL, OTHER };
  enum StatusCode { STATUS_OK, STATUS_ERROR };
  static constexpr uint8_t PICC_CMD_MF_AUTH_KEY_A=0x60;
  struct MIFARE_Key { uint8_t keyByte[6]; };
  struct Uid { uint8_t sak=0; } uid;
  PICC_Type type=PICC_TYPE_MIFARE_UL;
  std::vector<uint8_t> memory;
  bool fail=false;
  unsigned reads=0, auths=0;
  PICC_Type PICC_GetType(uint8_t) { return type; }
  StatusCode PCD_Authenticate(uint8_t,uint8_t,MIFARE_Key*,Uid*) { ++auths; return fail?STATUS_ERROR:STATUS_OK; }
  StatusCode MIFARE_Read(uint8_t address,uint8_t* out,uint8_t* size) {
    ++reads;
    size_t start=size_t(address)*(type==PICC_TYPE_MIFARE_UL?4:16);
    if(fail||*size<18||start+16>memory.size()) return STATUS_ERROR;
    memcpy(out,memory.data()+start,16);out[16]=0;out[17]=0;*size=18;return STATUS_OK;
  }
};
