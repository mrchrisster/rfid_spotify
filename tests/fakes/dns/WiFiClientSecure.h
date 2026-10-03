#pragma once
#include <stdint.h>
#include <string>
struct IPAddress {
  uint32_t value=0;
  IPAddress& operator=(uint32_t v) { value=v; return *this; }
};
class WiFiClientSecure {
protected:
  const char *_CA_cert="ca", *_cert="cert", *_private_key="key", *_pskIdent=nullptr, *_psKey=nullptr;
  int32_t _timeout=0;
public:
  virtual ~WiFiClientSecure()=default;
  virtual int connect(const char*,uint16_t) { ++unsafeCalls; return 0; }
  virtual int connect(const char*,uint16_t,int32_t) { ++unsafeCalls; return 0; }
  int connect(IPAddress address,uint16_t port,const char* host,const char* ca,const char* cert,const char* key) {
    ++tlsCalls; lastAddress=address.value; lastPort=port; lastHost=host;
    credentialsPreserved=ca==_CA_cert && cert==_cert && key==_private_key;
    return tlsResult;
  }
  int timeout() const { return _timeout; }
  unsigned unsafeCalls=0,tlsCalls=0; uint32_t lastAddress=0; uint16_t lastPort=0;
  std::string lastHost; bool credentialsPreserved=false; int tlsResult=1;
};
