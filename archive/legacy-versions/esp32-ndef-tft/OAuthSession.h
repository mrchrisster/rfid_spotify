#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace OAuth {
inline bool equal(const char* a, const char* b) {
  size_t n = strlen(a);
  if (n != strlen(b)) return false;
  unsigned diff = 0;
  for (size_t i = 0; i < n; ++i) diff |= unsigned(a[i] ^ b[i]);
  return diff == 0;
}
inline int hex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
inline bool decode(const char* begin, size_t length, char* out, size_t capacity) {
  size_t written = 0;
  for (size_t i = 0; i < length; ++i) {
    unsigned char c = begin[i];
    if (c == '%') {
      if (i + 2 >= length || hex(begin[i+1]) < 0 || hex(begin[i+2]) < 0) return false;
      c = (hex(begin[i+1]) << 4) | hex(begin[i+2]); i += 2;
    } else if (c == '+') c = ' ';
    if (c < 32 || c >= 127 || written + 1 >= capacity) return false;
    out[written++] = char(c);
  }
  out[written] = 0; return true;
}
// Reject duplicate keys (including percent-encoded aliases) and truncated values.
inline bool parameter(const char* query, const char* key, char* out, size_t capacity) {
  bool found = false; out[0] = 0;
  while (*query) {
    const char* end = strchr(query, '&'); if (!end) end = query + strlen(query);
    const char* eq = static_cast<const char*>(memchr(query, '=', end - query));
    if (!eq) return false;
    char name[64];
    if (!decode(query, eq-query, name, sizeof(name))) return false;
    if (!strcmp(name, key)) {
      if (found || !decode(eq+1, end-eq-1, out, capacity)) return false;
      found = true;
    }
    query = *end ? end+1 : end;
  }
  return found;
}
struct Session {
  char browser[45] = {}, state[45] = {}, verifier[45] = {};
  uint32_t issued = 0;
  bool active = false;
  bool valid(const char* suppliedState, const char* suppliedBrowser, uint32_t now) const {
    return active && uint32_t(now - issued) < 300000 && state[0] && browser[0] &&
      equal(state, suppliedState) && equal(browser, suppliedBrowser);
  }
  void consume() { active = false; memset(state, 0, sizeof(state)); memset(verifier, 0, sizeof(verifier)); }
};
}
