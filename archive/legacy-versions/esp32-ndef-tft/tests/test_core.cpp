#include "SafeNdef.h"
#include "Reliability.h"
#include <assert.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <random>

std::vector<uint8_t> textRecord(const std::string& text) {
  std::vector<uint8_t> result{0xd1, 1, uint8_t(text.size() + 3), 'T', 2, 'e', 'n'};
  result.insert(result.end(), text.begin(), text.end()); return result;
}
struct MemoryReader {
  std::vector<uint8_t> data;
  bool read(size_t offset, uint8_t* dest, size_t size) {
    if (offset > data.size() || size > data.size() - offset) return false;
    memcpy(dest, data.data() + offset, size); return true;
  }
};
int main() {
  char out[SafeNdef::MaxUri];
  const std::string uri = "spotify:album:0123456789ABCDEFGHIJKL";
  auto text = textRecord(uri);
  assert(SafeNdef::parse(text.data(), text.size(), out, sizeof(out)) && uri == out);
  auto url = textRecord("https://open.spotify.com/album/0123456789ABCDEFGHIJKL?si=ignored");
  assert(SafeNdef::parse(url.data(), url.size(), out, sizeof(out)) && uri == out);
  std::string suffix = "open.spotify.com/track/0123456789ABCDEFGHIJKL";
  std::vector<uint8_t> u{0xd1,1,uint8_t(suffix.size()+1),'U',4}; u.insert(u.end(),suffix.begin(),suffix.end());
  assert(SafeNdef::parse(u.data(), u.size(), out, sizeof(out)));
  assert(std::string(out) == "spotify:track:0123456789ABCDEFGHIJKL");
  for (size_t n=0;n<text.size();++n) assert(!SafeNdef::parse(text.data(), n, out, sizeof(out)));
  const uint8_t badLang[]{0xd1,1,1,'T',63}; assert(!SafeNdef::parse(badLang,sizeof(badLang),out,sizeof(out)));
  const uint8_t emptyText[]{0xd1,1,0,'T'}; assert(!SafeNdef::parse(emptyText,sizeof(emptyText),out,sizeof(out)));
  auto utf16=text; utf16[4]|=0x80; assert(!SafeNdef::parse(utf16.data(),utf16.size(),out,sizeof(out)));
  auto trailing=text; trailing.push_back(0); assert(!SafeNdef::parse(trailing.data(),trailing.size(),out,sizeof(out)));
  auto chunk=text; chunk[0]|=0x20; assert(!SafeNdef::parse(chunk.data(),chunk.size(),out,sizeof(out)));
  assert(!SafeNdef::normalize("spotify:album:bad",out,sizeof(out)));
  assert(!SafeNdef::normalize("https://evil.test/album/0123456789ABCDEFGHIJKL",out,sizeof(out)));
  assert(!SafeNdef::normalize(uri.c_str(),out,8));
  // Long-form payload lengths and five records (the old dependency overran at five).
  std::vector<uint8_t> multi;
  for (int i=0;i<5;++i) {
    auto r=text; r[0]=uint8_t(0x11 | (i==0?0x80:0) | (i==4?0x40:0)); multi.insert(multi.end(),r.begin(),r.end());
  }
  assert(SafeNdef::parse(multi.data(),multi.size(),out,sizeof(out)));
  std::vector<uint8_t> longForm{0xc1,1,0,0,0,text[2],'T'}; longForm.insert(longForm.end(),text.begin()+4,text.end());
  assert(SafeNdef::parse(longForm.data(),longForm.size(),out,sizeof(out)));
  longForm[2]=0xff; assert(!SafeNdef::parse(longForm.data(),longForm.size(),out,sizeof(out)));
  MemoryReader reader{{0,1,1,0xaa,3,uint8_t(text.size())}}; reader.data.insert(reader.data.end(),text.begin(),text.end()); reader.data.push_back(0xfe);
  assert(SafeNdef::read(reader,reader.data.size(),out,sizeof(out)));
  reader.data[5]=255; assert(!SafeNdef::read(reader,reader.data.size(),out,sizeof(out)));
  // Reproduce the failed artist B switch: no commit means A remains usable.
  AlbumOrder albums; uint16_t index;
  assert(!albums.current(index)); std::vector<uint16_t> a{7,9}; albums.commit(a);
  assert(albums.current(index)&&index==7); // failed fetch for B never calls commit
  albums.played(); assert(albums.current(index)&&index==9);
  albums.played(); assert(!albums.current(index)); albums.reshuffle([] { return 0U; }); assert(albums.current(index));
  albums.clear(); assert(!albums.current(index)); albums.played(); albums.reshuffle([] { return 0U; }); assert(!albums.current(index));
  assert(remainingDelay(10,0xfffffff0U,100)==74); assert(remainingDelay(101,0,100)==0);
  // Deterministic fuzzing under ASan/UBSan exercises malformed lengths and flags.
  std::mt19937 rng(42);
  for (int i=0;i<50000;++i) {
    std::vector<uint8_t> data(rng()%800);
    for(auto& byte:data) byte=rng();
    SafeNdef::parse(data.data(),data.size(),out,sizeof(out));
    MemoryReader fuzz{data}; SafeNdef::read(fuzz,data.size(),out,sizeof(out));
  }
  puts("Core regression tests and 50,000 malformed NDEF cases passed");
}
