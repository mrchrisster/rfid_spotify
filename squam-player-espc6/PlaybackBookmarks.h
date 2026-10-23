#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "SafeNdef.h"
// Network-worker owned. Fixed slots bound RAM/NVS; individual JSON records make
// checkpoints small and versioned. Never infer progress from elapsed wall time.
class PlaybackBookmarks {
public:
  static constexpr int Capacity=16;
  struct Entry { char card[48]={},context[48]={},item[48]={},artist[64]={},title[96]={}; uint32_t position=0,used=0; bool dirty=false,labelDirty=false; };
private:
  Entry entries[Capacity]; int active=-1; uint32_t serial=0;
  static bool canonical(const char* uri){char out[SafeNdef::MaxUri];return uri && strlen(uri)<48 && SafeNdef::normalize(uri,out,sizeof(out)) && !strcmp(uri,out);}
  static bool playable(const char* uri){return canonical(uri) && (!strncmp(uri,"spotify:album:",14)||!strncmp(uri,"spotify:playlist:",17)||!strncmp(uri,"spotify:track:",14)||!strncmp(uri,"spotify:episode:",16));}
  static bool itemUri(const char* uri){return canonical(uri) && (!strncmp(uri,"spotify:track:",14)||!strncmp(uri,"spotify:episode:",16));}
  static void key(int slot,char* dest){snprintf(dest,8,"b%02d",slot);}
public:
  const Entry* find(const char* card)const {for(const auto& e:entries)if(e.context[0] && !strcmp(e.card,card))return &e;return nullptr;}
  bool contains(const char* card)const {for(const auto& e:entries)if(e.card[0] && !strcmp(e.card,card))return true;return false;}
  void remember(const char* card){
    if(!canonical(card))return;
    for(auto& e:entries)if(!strcmp(e.card,card)){e.used=++serial;e.dirty=true;return;}
    int slot=-1;for(int i=0;i<Capacity;++i){if(i==active)continue;if(!entries[i].card[0]){slot=i;break;}if(slot<0 || entries[i].used<entries[slot].used)slot=i;}
    if(slot==active)active=-1;
    entries[slot]=Entry{};strcpy(entries[slot].card,card);entries[slot].used=++serial;entries[slot].dirty=true;
  }
  static void labelCopy(char* dest,size_t size,const char* text){
    if(!text)text="";size_t n=strlen(text);if(n>=size){n=size-1;while(n && (static_cast<unsigned char>(text[n])&0xc0)==0x80)--n;}
    memcpy(dest,text,n);dest[n]=0;
  }
  void label(const char* card,const char* artist,const char* title){for(auto& e:entries)if(!strcmp(e.card,card)){
    char nextArtist[64],nextTitle[96];labelCopy(nextArtist,sizeof(nextArtist),artist);labelCopy(nextTitle,sizeof(nextTitle),title);
    if(strcmp(e.artist,nextArtist)||strcmp(e.title,nextTitle)){strcpy(e.artist,nextArtist);strcpy(e.title,nextTitle);e.labelDirty=true;}return;
  }}
  String libraryJson()const {
    JsonDocument doc;auto list=doc["cards"].to<JsonArray>();
    for(const auto& e:entries)if(e.card[0]){auto item=list.add<JsonObject>();item["uri"]=e.card;item["artist"]=e.artist;item["title"]=e.title;item["used"]=e.used;}
    String out;serializeJson(doc,out);return out;
  }
  bool tracking()const{return active>=0;}
  void detach(){active=-1;}
  template<class Store> void load(Store& store){
    for(int i=0;i<Capacity;++i){
      char name[8];key(i,name);String data=store.getString(name,"");JsonDocument doc;
      if(data.length()>768 || deserializeJson(doc,data) || doc["v"]!=1)continue;
      const char* card=doc["card"]|"",*context=doc["context"]|"",*item=doc["item"]|"";
      if(!canonical(card)||(*context && !playable(context))||(*item && !itemUri(item))||!doc["ms"].is<uint32_t>()||!doc["used"].is<uint32_t>())continue;
      if(doc["ms"].as<uint32_t>()>2147483647U)continue;
      Entry& e=entries[i];strcpy(e.card,card);strcpy(e.context,context);strcpy(e.item,item);
      // Labels live separately: frequent progress checkpoints must not rewrite names.
      char labelKey[8];snprintf(labelKey,sizeof(labelKey),"l%02d",i);JsonDocument labels;String labelData=store.getString(labelKey,"");
      if(labelData.length()<=1536 && !deserializeJson(labels,labelData) && labels["card"].as<String>()==card){
        labelCopy(e.artist,sizeof(e.artist),labels["artist"]|"");labelCopy(e.title,sizeof(e.title),labels["title"]|"");
      }
      e.position=doc["ms"];e.used=doc["used"];if(e.used>serial)serial=e.used;
    }
  }
  template<class Store> bool save(Store& store){
    bool success=true;
    for(int i=0;i<Capacity;++i){auto& e=entries[i];
      if(e.labelDirty){JsonDocument labels;labels["card"]=e.card;labels["artist"]=e.artist;labels["title"]=e.title;
        String data;serializeJson(labels,data);char name[8];snprintf(name,sizeof(name),"l%02d",i);
        if(store.putString(name,data)==data.length())e.labelDirty=false;else success=false;
      }
      if(!e.dirty)continue;
      JsonDocument doc;doc["v"]=1;doc["card"]=e.card;doc["context"]=e.context;doc["item"]=e.item;doc["ms"]=e.position;doc["used"]=e.used;
      String data;serializeJson(doc,data);char name[8];key(i,name);
      if(store.putString(name,data)==data.length())e.dirty=false;else success=false;
    }
    return success;
  }
  void started(const char* card,const char* context,const char* item,uint32_t position){
    if(!canonical(card)||!playable(context)||(*item && !itemUri(item)))return;
    int slot=-1;
    for(int i=0;i<Capacity;++i)if(!strcmp(entries[i].card,card)){slot=i;break;}
    if(slot<0){slot=0;for(int i=0;i<Capacity;++i){if(!entries[i].card[0]){slot=i;break;}if(entries[i].used<entries[slot].used)slot=i;}}
    auto& e=entries[slot];if(strcmp(e.card,card)){e=Entry{};}
    // Clear the old album label only after new playback succeeds; failures retain it.
    if(e.context[0] && strcmp(e.context,context) && (e.artist[0] || e.title[0])){e.artist[0]=0;e.title[0]=0;e.labelDirty=true;}
    strcpy(e.card,card);strcpy(e.context,context);strcpy(e.item,item);
    e.position=position;e.used=++serial;e.dirty=true;active=slot;
  }
  bool capture(JsonDocument& doc,const String& device){
    if(!tracking() || device.isEmpty() || doc["device"]["id"].as<String>()!=device || !doc["progress_ms"].is<uint32_t>())return false;
    auto& e=entries[active];String item=doc["item"]["uri"].as<String>();
    if(!itemUri(item.c_str()))return false;
    bool matches=false;
    const bool showCard=!strncmp(e.card,"spotify:show:",13);
    if(showCard){
      // Advance only on observed same-show episodes from the selected speaker;
      // a manual pause merely updates position, never issues playback commands.
      matches=item.startsWith("spotify:episode:") && doc["item"]["show"]["uri"].as<String>()==e.card;
    }
    else if(!strncmp(e.context,"spotify:album:",14))matches=doc["item"]["album"]["uri"].as<String>()==e.context && item.startsWith("spotify:track:");
    else if(!strncmp(e.context,"spotify:playlist:",17))matches=doc["context"]["uri"].as<String>()==e.context && item.startsWith("spotify:track:");
    else matches=item==e.context;
    uint32_t position=doc["progress_ms"];
    if(!matches || position>2147483647U)return false;
    if(doc["item"]["duration_ms"].is<uint32_t>() && position>=doc["item"]["duration_ms"].as<uint32_t>())return false;
    if(showCard && strcmp(e.context,item.c_str())){strcpy(e.context,item.c_str());e.dirty=true;}
    if(strcmp(e.item,item.c_str()) || e.position!=position){strcpy(e.item,item.c_str());e.position=position;e.dirty=true;}
    return true;
  }
};
