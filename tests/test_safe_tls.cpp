#include "SafeTlsClient.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int resolverError=0, resolves=0, frees=0;
static bool malformed=false;
static sockaddr_in address{};
static addrinfo answer{};
int lwip_getaddrinfo(const char* host,const char*,const addrinfo* hints,addrinfo** out) {
  assert(strcmp(host,"accounts.spotify.com")==0);
  assert(hints->ai_family==AF_INET && hints->ai_socktype==SOCK_STREAM);
  ++resolves;
  if(resolverError) { *out=nullptr; return resolverError; }
  address.sin_addr.s_addr=0x04030201;
  answer.ai_family=AF_INET;answer.ai_addrlen=sizeof(address);
  answer.ai_addr=malformed?nullptr:reinterpret_cast<sockaddr*>(&address);
  *out=&answer;return 0;
}
void lwip_freeaddrinfo(addrinfo* p) { assert(p==&answer); ++frees; }
int main() {
  SafeTlsClient client; WiFiClientSecure& dispatch=client;
  assert(dispatch.connect("accounts.spotify.com",443,3000)==1);
  assert(client.unsafeCalls==0 && client.tlsCalls==1 && client.timeout()==3000);
  assert(client.lastHost=="accounts.spotify.com" && client.lastAddress==0x04030201);
  assert(client.lastPort==443 && client.credentialsPreserved && frees==1);
  // TLS verification failures must propagate, never fall back to insecure TLS.
  client.tlsResult=0; assert(dispatch.connect("accounts.spotify.com",443)==0);
  assert(resolves==2 && frees==2 && client.tlsCalls==2);
  resolverError=EAI_FAIL;assert(dispatch.connect("accounts.spotify.com",443)==0);
  assert(client.tlsCalls==2 && client.unsafeCalls==0);
  resolverError=0;malformed=true;assert(dispatch.connect("accounts.spotify.com",443)==0);
  assert(frees==3 && client.tlsCalls==2);
  assert(dispatch.connect(nullptr,443)==0);assert(dispatch.connect("",443)==0);
  puts("Safe DNS dispatch, TLS hostname/credentials and failure propagation passed");
}
