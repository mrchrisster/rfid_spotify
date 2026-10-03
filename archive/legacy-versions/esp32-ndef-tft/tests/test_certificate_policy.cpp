#include "CertificatePolicy.h"
#include <assert.h>
#include <string>
#include <stdio.h>
struct Store {
  std::string disk="old"; bool fail=false, torn=false;
  bool write(const std::string& s) { if (fail) return false; disk=torn ? "corrupt" : s; return true; }
  std::string read() { return disk; }
};
int main() {
  using namespace CertificatePolicy;
  int64_t now=utcEpoch(2026,9,20,0,0,0);
  assert(utcEpoch(1970,1,1,0,0,0)==0);
  assert(utcEpoch(2000,3,1,0,0,0)-utcEpoch(2000,2,28,0,0,0)==2*Day);
  assert(utcEpoch(2100,3,1,0,0,0)-utcEpoch(2100,2,28,0,0,0)==Day);
  assert(utcEpoch(2038,1,19,3,14,8)==2147483648LL);
  assert(!due(0,0)); // Never sign against an unsynchronized clock.
  assert(!due(now,now+RenewBefore+1));
  assert(due(now,now+RenewBefore) && due(now,now-2*365*Day));
  assert(nextExpiry(now,now+10*365*Day)==now+Lifetime);
  assert(nextExpiry(now,now+100*Day)==now+99*Day);
  std::string active="old"; Store store;
  store.fail=true; assert(!commit(store,std::string("new"),active) && active=="old" && store.disk=="old");
  store.fail=false;store.torn=true; assert(!commit(store,std::string("new"),active) && active=="old");
  store.torn=false; assert(commit(store,std::string("new"),active) && active=="new" && store.disk=="new");
  puts("Certificate expiry/UTC and commit-failure regression tests passed");
}
