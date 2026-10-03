#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Allocation-free parser. Complete envelope validation precedes use of a URI.
namespace SafeNdef {
constexpr size_t MaxMessage = 768;
constexpr size_t MaxUri = 160;
inline bool normalize(const char* raw, char* out, size_t capacity) {
  const char* p = raw;
  if (strncmp(p, "https://open.spotify.com/", 25) == 0) p += 25;
  else if (strncmp(p, "spotify:", 8) == 0) p += 8;
  else return false;
  const char* end = p;
  while (*end && *end != '?' && *end != '#') ++end;
  const char* sep = p;
  while (sep < end && *sep != '/' && *sep != ':') ++sep;
  size_t type = sep - p;
  bool validType = (type == 5 && !memcmp(p, "album", 5)) ||
    (type == 6 && !memcmp(p, "artist", 6)) || (type == 8 && !memcmp(p, "playlist", 8)) ||
    (type == 5 && !memcmp(p, "track", 5));
  if (!validType || sep == end || end - sep - 1 != 22 || 8 + type + 1 + 22 + 1 > capacity) return false;
  for (const char* c = sep + 1; c < end; ++c)
    if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9'))) return false;
  memcpy(out, "spotify:", 8); memcpy(out + 8, p, type); out[8 + type] = ':';
  memcpy(out + 9 + type, sep + 1, 22); out[31 + type] = 0;
  return true;
}
inline bool parse(const uint8_t* bytes, size_t size, char* uri, size_t capacity) {
  if (!bytes || !uri || !capacity || !size || size > MaxMessage) return false;
  uri[0] = 0;
  char candidate[MaxUri] = {};
  size_t pos = 0, records = 0;
  bool ended = false;
  while (pos < size) {
    if (++records > 16 || size - pos < 3) return false;
    uint8_t flags = bytes[pos++], typeLen = bytes[pos++];
    if ((flags & 0x20) || (flags & 7) >= 6 || bool(flags & 0x80) != (records == 1)) return false;
    uint32_t payloadLen = 0;
    if (flags & 0x10) payloadLen = bytes[pos++];
    else {
      if (size - pos < 4) return false;
      for (int i = 0; i < 4; ++i) payloadLen = (payloadLen << 8) | bytes[pos++];
    }
    uint8_t idLen = 0;
    if (flags & 8) { if (pos == size) return false; idLen = bytes[pos++]; }
    if (typeLen > size - pos) return false;
    const uint8_t* type = bytes + pos; pos += typeLen;
    if (idLen > size - pos) return false;
    pos += idLen;
    if (payloadLen > size - pos) return false;
    const uint8_t* payload = bytes + pos; pos += payloadLen;
    if ((flags & 7) == 0 && (typeLen || idLen || payloadLen)) return false;
    if ((flags & 7) == 1 && typeLen == 1 && (type[0] == 'T' || type[0] == 'U')) {
      if (!payloadLen) return false;
      size_t skip = 1; const char* prefix = "";
      if (type[0] == 'T') {
        if (payload[0] & 0xC0) return false; // UTF-16/reserved status unsupported
        skip += payload[0] & 0x3f;
        if (skip > payloadLen) return false;
      } else {
        if (payload[0] == 4) prefix = "https://";
        else if (payload[0] == 3) prefix = "http://";
        else if (payload[0] != 0) return false;
      }
      size_t len = payloadLen - skip, prefixLen = strlen(prefix);
      if (len + prefixLen >= MaxUri || memchr(payload + skip, 0, len)) return false;
      char raw[MaxUri]; memcpy(raw, prefix, prefixLen); memcpy(raw + prefixLen, payload + skip, len);
      raw[len + prefixLen] = 0;
      if (!candidate[0]) normalize(raw, candidate, sizeof(candidate));
    }
    if (flags & 0x40) { ended = pos == size; break; }
  }
  if (!ended || !candidate[0] || strlen(candidate) >= capacity) return false;
  strcpy(uri, candidate); return true;
}

// Reader supplies bounded logical tag data; skip optional TLVs safely.
template<class Reader> bool read(Reader& reader, size_t capacity, char* uri, size_t uriCapacity) {
  uint8_t message[MaxMessage];
  size_t pos = 0;
  while (pos < capacity) {
    uint8_t type, length;
    if (!reader.read(pos++, &type, 1)) return false;
    if (type == 0) continue;
    if (type == 0xfe) return false;
    if (pos == capacity || !reader.read(pos++, &length, 1)) return false;
    size_t size = length;
    if (length == 0xff) {
      uint8_t wide[2];
      if (capacity - pos < 2 || !reader.read(pos, wide, 2)) return false;
      pos += 2; size = (size_t(wide[0]) << 8) | wide[1];
    }
    if (size > capacity - pos) return false;
    if (type == 3) {
      if (!size || size > sizeof(message) || !reader.read(pos, message, size)) return false;
      return parse(message, size, uri, uriCapacity);
    }
    pos += size;
  }
  return false;
}
}
