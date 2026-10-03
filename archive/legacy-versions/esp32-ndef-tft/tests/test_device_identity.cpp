#include "DeviceIdentity.cpp"
#include <assert.h>
#include <stdio.h>
#include <fstream>
uint32_t testMillis=1;
void logMessage(const String& message) { puts(message.c_str()); }
void reboot() {
  DeviceIdentity::persisted=false; DeviceIdentity::checked=false;
  DeviceIdentity::begin();
}
int main(int argc,char** argv) {
  using namespace DeviceIdentity;
  assert(DEVICE_AUTO_CERTIFICATE);
  begin();
  assert(automatic() && !persisted && issuerDays()>3000);
  String originalChain=chain();
  // The first synchronized boot actually signs + validates + persists a new leaf.
  assert(maintain() && persisted);
  assert(String(chain())==originalChain); // Borrowed server buffer is not changed while server runs.
  int64_t expiry=0; assert(validate(leaf.c_str(),expiry));
  assert(expiry>time(nullptr)+390*CertificatePolicy::Day);
  activate(); String renewedChain=chain();
  { Crypto parsed; assert(parseCert(&parsed.cert,chain())==0 && parsed.cert.next); }
  assert(renewedChain!=originalChain);
  reboot(); assert(persisted && String(chain())==renewedChain);
  assert(!maintain()); // No flash wear/signing on each restart.
  assert(!validate("corrupt PEM",expiry));
  // A certificate made with an incorrect future clock is replaced after time recovers.
  validFrom=time(nullptr)+10*CertificatePolicy::Day; checked=false;
  assert(maintain()); activate(); renewedChain=chain();
  // Simulate returning with an expired leaf. A failed flash write keeps the current chain.
  expires=time(nullptr)-365*CertificatePolicy::Day; checked=false;
  FakeNvs::failWrite=true;
  String old=leaf; assert(!maintain() && leaf==old && String(chain())==renewedChain);
  FakeNvs::failWrite=false; checked=false;
  assert(maintain()); activate();
  assert(certificateDays()>=396);
  // Do not issue certificates beyond the issuer's validity or pretend to extend trust.
  String candidate; assert(!generate(issuerExpires-20*CertificatePolicy::Day,candidate,expiry));
  assert(generate(time(nullptr)+400*CertificatePolicy::Day,candidate,expiry));
  assert(validate(candidate.c_str(),expiry)); // Validated even when dates are in the future.
  if(argc>1) { std::ofstream out(argv[1]);out<<chain(); }
  puts("Real Mbed TLS certificate generation, validation, restart and persistence tests passed");
}
