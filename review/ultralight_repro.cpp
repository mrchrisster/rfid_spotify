#include <cstdint>
#include <cstring>
using byte = uint8_t;
#define ULTRALIGHT_DATA_START_PAGE 4
#define ULTRALIGHT_MAX_PAGE 63
#define ULTRALIGHT_READ_SIZE 16
#define ULTRALIGHT_PAGE_SIZE 4
struct NdefMessage { void addEmptyRecord() {} };
struct NfcTag { enum {TYPE_2}; template<class... T> NfcTag(T...) {} };
struct MFRC522 {
 enum StatusCode { STATUS_OK };
 struct { byte uidByte[7] = {}; byte size=7; } uid;
 StatusCode MIFARE_Read(byte, byte* dst, byte* n) { memset(dst,0,*n); return STATUS_OK; }
};
struct MifareUltralight {
 MFRC522* nfc;
 bool isUnformatted() { return false; }
 void findNdefMessage(uint16_t* len,uint16_t* start) { *len=40; *start=2; }
 uint16_t calculateBufferSize(uint16_t,uint16_t);
 NfcTag read();
};
NfcTag MifareUltralight::read()
{
    if (isUnformatted())
    {
#ifdef NDEF_USE_SERIAL
        Serial.println(F("WARNING: Tag is not formatted."));
#endif
        return NfcTag(nfc->uid.uidByte, nfc->uid.size, NfcTag::TYPE_2);
    }

    uint16_t messageLength = 0;
    uint16_t ndefStartIndex = 0;
    findNdefMessage(&messageLength, &ndefStartIndex);

    uint16_t bufferSize = calculateBufferSize(messageLength, ndefStartIndex);

    if (messageLength == 0) { // data is 0x44 0x03 0x00 0xFE
        NdefMessage message = NdefMessage();
        message.addEmptyRecord();
        return NfcTag(nfc->uid.uidByte, nfc->uid.size, NfcTag::TYPE_2, message);
    }

    uint8_t index = 0;
    byte buffer[bufferSize];
    for (uint8_t page = ULTRALIGHT_DATA_START_PAGE; page < ULTRALIGHT_MAX_PAGE; page+=(ULTRALIGHT_READ_SIZE/ULTRALIGHT_PAGE_SIZE))
    {
        // read the data
        byte dataSize = ULTRALIGHT_READ_SIZE + 2;
        MFRC522::StatusCode status = nfc->MIFARE_Read(page, &buffer[index], &dataSize);
        if (status == MFRC522::STATUS_OK)
        {
            #ifdef MIFARE_ULTRALIGHT_DEBUG
            Serial.print(F("Page "));Serial.print(page);Serial.print(" ");
            PrintHexChar(&buffer[index], ULTRALIGHT_PAGE_SIZE);
            PrintHexChar(&buffer[index+ULTRALIGHT_PAGE_SIZE], ULTRALIGHT_PAGE_SIZE);
            PrintHexChar(&buffer[index+2*ULTRALIGHT_PAGE_SIZE], ULTRALIGHT_PAGE_SIZE);
            PrintHexChar(&buffer[index+3*ULTRALIGHT_PAGE_SIZE], ULTRALIGHT_PAGE_SIZE);
            #endif
        }
        else
        {
#ifdef NDEF_USE_SERIAL
            Serial.print(F("Read failed "));Serial.println(page);
#endif
            return NfcTag(nfc->uid.uidByte, nfc->uid.size, NfcTag::TYPE_2);
        }

        if (index >= (messageLength + ndefStartIndex))
        {
            break;
        }

        index += ULTRALIGHT_READ_SIZE;
    }

    return NfcTag(nfc->uid.uidByte, nfc->uid.size, NfcTag::TYPE_2, &buffer[ndefStartIndex], messageLength);

}
uint16_t MifareUltralight::calculateBufferSize(uint16_t messageLength, uint16_t ndefStartIndex)
{
    // TLV terminator 0xFE is 1 byte
    uint16_t bufferSize = messageLength + ndefStartIndex + 1;

    if (bufferSize % ULTRALIGHT_READ_SIZE != 0)
    {
        // buffer must be an increment of page size
        bufferSize = ((bufferSize / ULTRALIGHT_READ_SIZE) + 1) * ULTRALIGHT_READ_SIZE;
    }

    //MFRC522 also return CRC
    bufferSize += 2;

    return bufferSize;
}
int main() { MFRC522 chip; MifareUltralight reader{&chip}; reader.read(); }
