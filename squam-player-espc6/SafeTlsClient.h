#pragma once
#include <WiFiClientSecure.h>
#if defined(ARDUINO_ARCH_ESP32)
#include <lwip/netdb.h>

// Arduino-ESP32 3.3.11 Network.hostByName clears DNS outside the TCP/IP core.
// With pending SNTP DNS this can invoke raw UDP callbacks in our worker and
// assert. Use the socket resolver instead; retain the ORIGINAL hostname for
// TLS SNI and certificate verification. Never hold a core lock across DNS/TLS.
class SafeTlsClient : public WiFiClientSecure {
  int dnsFailure=0;
public:
  int lastDnsError() const { return dnsFailure; }
  using WiFiClientSecure::connect;
  int connect(const char* host, uint16_t port) override {
    dnsFailure=0;
    if (!host || !*host) {dnsFailure=-1;return 0;}
    // This adapter is for certificate-based HTTPS, not PSK connections.
    if (_pskIdent || _psKey) return 0;
    addrinfo hints{}, *result = nullptr;
    hints.ai_family = AF_INET; // Player uses IPv4 Wi-Fi.
    hints.ai_socktype = SOCK_STREAM;
    int error = lwip_getaddrinfo(host, nullptr, &hints, &result);
    if (error || !result) {
      dnsFailure=error?error:-1;
      if (result) lwip_freeaddrinfo(result);
      return 0;
    }
    bool valid = result->ai_family == AF_INET && result->ai_addr &&
                 result->ai_addrlen >= sizeof(sockaddr_in);
    IPAddress address;
    if (valid) address = reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr.s_addr;
    lwip_freeaddrinfo(result);
    if (!valid) {dnsFailure=-1;return 0;}
    return WiFiClientSecure::connect(address, port, host, _CA_cert, _cert, _private_key);
  }
  int connect(const char* host, uint16_t port, int32_t timeout) override {
    _timeout = timeout;
    return connect(host, port);
  }
};
#else
// Native transport tests replace the TLS client with a mock.
using SafeTlsClient = WiFiClientSecure;
#endif
