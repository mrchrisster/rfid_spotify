#include "PlayerLanguage.h"
#include <assert.h>
#include <stdio.h>
#include <initializer_list>
int main() {
  assert(PlayerLanguage::parse(nullptr)==-1);
  assert(PlayerLanguage::parse("xx")==-1);
  assert(PlayerLanguage::parse("de ")==-1);
  assert(strcmp(PlayerLanguage::code(255),"de")==0);
  for(uint8_t id=0;id<PlayerLanguage::Count;++id) {
    assert(PlayerLanguage::parse(PlayerLanguage::code(id))==id);
    const auto& c=PlayerLanguage::get(id);
    for(unsigned state=0;state<6;++state) {
      assert(PlayerLanguage::length(c.title[state])*12<=320);
      assert(PlayerLanguage::length(c.hint[state])*6<=320);
      for(const char* s:{c.title[state],c.hint[state]}) {
        while(*s) { bool question=*s=='?';auto ch=PlayerLanguage::glyph(s);assert(ch!='?'||question); }
      }
    }
    assert((strlen(c.reader)+1+strlen(c.wait))*6<=80);
    assert(PlayerLanguage::length(c.noAddress)*6<=320);
  }
  const char* accented="äöüßéèêàçñíóúáîïâÉ¡¿";
  const uint8_t expected[]={132,148,129,225,130,138,136,133,135,164,161,162,163,160,140,139,131,144,173,168};
  assert(PlayerLanguage::length(accented)==sizeof(expected));
  for(auto value:expected)assert(PlayerLanguage::glyph(accented)==value);
  assert(*accented==0);
  puts("Language selection, screen fit and accented TFT glyph tests passed");
}
