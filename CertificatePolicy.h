#pragma once
#include <stdint.h>
namespace CertificatePolicy {
constexpr int64_t Day = 86400;
constexpr int64_t RenewBefore = 90 * Day;
constexpr int64_t Lifetime = 397 * Day;
inline int64_t utcEpoch(int y, int m, int d, int hour, int minute, int second) {
  static const int beforeMonth[] = {0,31,59,90,120,151,181,212,243,273,304,334};
  if (y < 1970 || y > 9999 || m < 1 || m > 12 || d < 1 || d > 31) return 0;
  int64_t days = int64_t(y-1970)*365 + ((y-1)/4-1969/4) - ((y-1)/100-1969/100) + ((y-1)/400-1969/400);
  days += beforeMonth[m-1]+d-1;
  if (m>2 && y%4==0 && (y%100!=0 || y%400==0)) ++days;
  return days*Day+hour*3600+minute*60+second;
}
inline bool timeReady(int64_t now) { return now >= 1700000000; }
inline bool due(int64_t now, int64_t expiry) { return timeReady(now) && expiry - now <= RenewBefore; }
inline int64_t nextExpiry(int64_t now, int64_t issuerExpiry) {
  int64_t requested = now + Lifetime;
  return requested < issuerExpiry - Day ? requested : issuerExpiry - Day;
}
// Persist and verify before changing the certificate served in RAM. NVS's single-key
// update is atomic: power loss leaves either the old complete PEM or the new PEM.
template<class Store, class Text>
bool commit(Store& store, const Text& candidate, Text& active) {
  if (!store.write(candidate) || store.read() != candidate) return false;
  active = candidate; return true;
}
}
