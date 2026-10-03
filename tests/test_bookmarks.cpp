#include "PlaybackBookmarks.h"
#include "CardRestart.h"
#include "BookmarkPolicy.h"
#include "CardLibraryMetadata.h"
#include <cassert>
#include <map>
#include <string>
#include <iostream>
struct Store {
  std::map<std::string,String> values;bool fail=false;unsigned writes=0;
  String getString(const char* key,const char* fallback){auto i=values.find(key);return i==values.end()?String(fallback):i->second;}
  size_t putString(const char* key,const String& value){++writes;if(fail)return 0;values[key]=value;return value.length();}
};
const char* album="spotify:album:0123456789ABCDEFGHIJKL";
const char* artist="spotify:artist:0123456789ABCDEFGHIJKL";
const char* track="spotify:track:0123456789ABCDEFGHIJKL";
const char* episode="spotify:episode:0123456789ABCDEFGHIJKL";
const char* show="spotify:show:0123456789ABCDEFGHIJKL";
int main(){
  assert(!BookmarkPolicy::captureDue(29999,0));assert(BookmarkPolicy::captureDue(30000,0));
  assert(!BookmarkPolicy::saveDue(60000,0));assert(!BookmarkPolicy::saveDue(299999,0));assert(BookmarkPolicy::saveDue(300000,0));
  assert(!BookmarkPolicy::saveDue(298998,UINT32_MAX-1000));assert(BookmarkPolicy::saveDue(298999,UINT32_MAX-1000));

  unsigned periodicSaves=0;uint32_t lastSaved=0;for(uint32_t now=30000;now<=2*60*60*1000;now+=30000)if(BookmarkPolicy::saveDue(now,lastSaved)){++periodicSaves;lastSaved=now;}assert(periodicSaves==24);
  CardRestart gesture;uint8_t a[]={1,2,3,4},b[]={5,6,7,8};
  assert(!gesture.present(a,4,album,0));assert(gesture.present(a,4,album,60000)); // A minute to decide.
  gesture.clear();assert(!gesture.present(a,4,album,0));assert(gesture.present(a,4,album,120000)); // Inclusive two-minute boundary.
  gesture.clear();assert(!gesture.present(a,4,album,0));
  assert(gesture.present(a,4,album,120001)); // No album timeout.
  assert(!gesture.present(b,4,album,21000));assert(!gesture.present(a,4,album,22000));
  assert(!gesture.present(a,4,episode,23000)); // Rewritten card isn't a repeat book.
  gesture.clear();assert(!gesture.present(a,4,episode,24000));
  gesture.clear();assert(!gesture.present(a,4,album,UINT32_MAX-500));assert(gesture.present(a,4,album,500));
  gesture.clear();assert(!CardRestart::shuffles(gesture.present(a,4,artist,0),artist));
  assert(CardRestart::shuffles(gesture.present(a,4,artist,60000),artist));
  assert(CardRestart::shuffles(gesture.present(a,4,artist,180001),artist));
  gesture.clear();assert(!CardRestart::shuffles(gesture.present(a,4,artist,0),artist));
  assert(CardRestart::shuffles(gesture.present(a,4,artist,120000),artist));
  assert(CardRestart::shuffles(true,album));assert(!CardRestart::shuffles(true,show));assert(!CardRestart::shuffles(true,episode));
  assert(!CardRestart::shuffles(true,track));assert(!CardRestart::shuffles(true,nullptr));
  assert(!CardRestart::shuffles(false,album));
  gesture.clear();assert(!CardRestart::shuffles(gesture.present(a,4,album,0),album));
  assert(CardRestart::shuffles(gesture.present(a,4,album,86400000),album));
  assert(!gesture.present(b,4,artist,86400001));assert(!gesture.present(a,4,album,86400002)); // A→B→A resumes.
  gesture.clear();assert(!gesture.present(a,4,album,86400003)); // Reboot/reset resumes.
  gesture.clear();assert(!gesture.present(a,4,show,0));assert(!gesture.present(a,4,show,120001));
  gesture.clear();assert(!gesture.present(a,4,episode,0));assert(gesture.present(a,4,episode,60000));
  PlaybackBookmarks albumResume;Store albumStore;
  albumResume.started(album,album,track,45000);assert(albumResume.tracking());
  assert(albumResume.save(albumStore));
  JsonDocument albumState;albumState["device"]["id"]="Echo";albumState["item"]["uri"]=track;
  albumState["item"]["album"]["uri"]=album;albumState["progress_ms"]=60000;
  assert(albumResume.capture(albumState,"Echo"));assert(albumResume.save(albumStore));
  unsigned albumWrites=albumStore.writes;
  assert(albumResume.capture(albumState,"Echo"));assert(albumResume.save(albumStore));assert(albumStore.writes==albumWrites);
  PlaybackBookmarks albumReboot;albumReboot.load(albumStore);
  assert(albumReboot.find(album)->position==60000 && !strcmp(albumReboot.find(album)->item,track));
  PlaybackBookmarks books;Store store;
  books.started(artist,album,"",0);assert(books.tracking());assert(books.save(store));
  assert(store.writes==1);assert(books.save(store) && store.writes==1);
  JsonDocument state;state["device"]["id"]="Echo";state["item"]["uri"]=track;
  state["item"]["album"]["uri"]=album;state["item"]["duration_ms"]=120000;state["progress_ms"]=45000;
  assert(!books.capture(state,"other"));assert(books.find(artist)->position==0);
  assert(books.capture(state,"Echo"));assert(books.find(artist)->position==45000);
  assert(!strcmp(books.find(artist)->context,album) && !strcmp(books.find(artist)->item,track));
  store.fail=true;assert(!books.save(store));store.fail=false;assert(books.save(store));
  const auto stableWrites=store.writes;assert(books.capture(state,"Echo"));assert(books.save(store));assert(store.writes==stableWrites); // Paused/unchanged playback causes no NVS write.
  PlaybackBookmarks reboot;reboot.load(store);assert(!reboot.tracking());
  assert(reboot.find(artist)->position==45000);assert(!reboot.capture(state,"Echo"));
  // Other playback, missing/negative progress and exhausted item must not overwrite bookmarks.
  state["item"]["album"]["uri"]="unrelated";assert(!books.capture(state,"Echo"));
  state["item"]["album"]["uri"]=album;state["progress_ms"]=nullptr;assert(!books.capture(state,"Echo"));
  state["progress_ms"]=-1;assert(!books.capture(state,"Echo"));
  state["progress_ms"]=120000;assert(!books.capture(state,"Echo"));
  books.started(show,episode,"",0);state["item"]["show"]["uri"]=show;state["item"]["uri"]=episode;state["progress_ms"]=30000;
  assert(books.capture(state,"Echo"));assert(books.find(show)->position==30000);
  state["item"]["uri"]="spotify:episode:ABCDEFGHIJKLMNOPQRSTUV";state["progress_ms"]=1234;state["is_playing"]=false;
  assert(books.capture(state,"Echo"));assert(!strcmp(books.find(show)->context,"spotify:episode:ABCDEFGHIJKLMNOPQRSTUV") && books.find(show)->position==1234);
  assert(books.save(store));PlaybackBookmarks progressed;progressed.load(store);assert(progressed.find(show)->position==1234);
  state["item"]["show"]["uri"]="spotify:show:ABCDEFGHIJKLMNOPQRSTUV";state["progress_ms"]=5678;
  assert(!books.capture(state,"Echo") && books.find(show)->position==1234);
  state["item"]["uri"]=track;assert(!books.capture(state,"Echo"));
  books.detach();assert(!books.capture(state,"Echo"));
  const char* playlist="spotify:playlist:0123456789ABCDEFGHIJKL";
  books.started(playlist,playlist,"",0);assert(!books.capture(state,"Echo"));
  state["context"]["uri"]=playlist;assert(books.capture(state,"Echo"));
  books.started(artist,album,"",0);assert(books.find(artist)->position==0); // Explicitly starting at zero replaces prior progress.
  assert(books.save(store));PlaybackBookmarks reset;reset.load(store);assert(reset.find(artist)->position==0);
  Store broken;broken.values["b00"]="{bad";broken.values["b01"]=R"({"v":1,"card":"bad","context":"bad","ms":0,"used":1})";
  PlaybackBookmarks corrupt;corrupt.load(broken);assert(!corrupt.find(artist));
  PlaybackBookmarks bounded;
  for(int i=0;i<PlaybackBookmarks::Capacity+1;++i){std::string card=album;card.back()=char('A'+i);bounded.started(card.c_str(),album,"",0);}
  std::string oldest=album;oldest.back()='A';assert(!bounded.find(oldest.c_str()));
  PlaybackBookmarks library;Store libraryStore;library.remember(album);assert(library.contains(album)&&!library.find(album));
  library.label(album,"Artist","Book");assert(library.save(libraryStore));unsigned writes=libraryStore.writes;
  library.label(album,"Artist","Book");assert(library.save(libraryStore)&&libraryStore.writes==writes);
  PlaybackBookmarks loaded;loaded.load(libraryStore);assert(loaded.contains(album)&&!loaded.find(album));
  JsonDocument list;assert(!deserializeJson(list,loaded.libraryJson()));assert(list["cards"][0]["title"]=="Book");
  loaded.started(album,album,"",0);assert(loaded.save(libraryStore));assert(libraryStore.writes==writes+1); // Progress does not rewrite labels.
  loaded.capture(state,"Echo");loaded.save(libraryStore);
  char utf[4];PlaybackBookmarks::labelCopy(utf,sizeof(utf),"abé");assert(!strcmp(utf,"ab"));
  loaded.label(album,"Changed","New book");libraryStore.fail=true;assert(!loaded.save(libraryStore));libraryStore.fail=false;assert(loaded.save(libraryStore));
  PlaybackBookmarks relabeled;relabeled.load(libraryStore);assert(!strcmp(relabeled.find(album)->title,"New book"));
  // An unplayed new scan must not evict the currently tracked book.
  for(int i=0;i<PlaybackBookmarks::Capacity+2;++i){std::string card=artist;card.back()=char('A'+i);loaded.remember(card.c_str());}
  assert(loaded.find(album));assert(!deserializeJson(list,loaded.libraryJson()));assert(list["cards"].size()==16);
  assert(loaded.save(libraryStore));PlaybackBookmarks evicted;evicted.load(libraryStore);assert(evicted.contains(album));
  JsonDocument metadata;metadata["name"]="Album title";metadata["artists"][0]["name"]="Album artist";
  assert(CardLibraryMetadata::apply(loaded,album,album,metadata));assert(!strcmp(loaded.find(album)->title,"Album title"));
  metadata.clear();metadata["item"]["album"]["uri"]="wrong";metadata["item"]["album"]["name"]="Wrong album";
  assert(!CardLibraryMetadata::apply(loaded,album,album,metadata));assert(!strcmp(loaded.find(album)->title,"Album title"));
  metadata["item"]["album"]["uri"]=album;metadata["item"]["album"]["artists"][0]["name"]="Track artist";
  assert(CardLibraryMetadata::apply(loaded,album,album,metadata));assert(!strcmp(loaded.find(album)->artist,"Track artist"));
  loaded.remember(show);loaded.label(show,"Publisher","Vorspann");metadata.clear();metadata["name"]="Episode title";metadata["show"]["publisher"]="Publisher";metadata["show"]["name"]="Der kleine Drache Kokosnuss";metadata["show"]["uri"]=show;
  assert(CardLibraryMetadata::apply(loaded,show,episode,metadata));
  assert(!deserializeJson(list,loaded.libraryJson()));bool episodeNamed=false;for(JsonObject card:list["cards"].as<JsonArray>())if(card["uri"]==show)episodeNamed=card["artist"]=="Publisher"&&card["title"]=="Der kleine Drache Kokosnuss";assert(episodeNamed);
  metadata["show"]["uri"]="spotify:show:ABCDEFGHIJKLMNOPQRSTUV";
  assert(!CardLibraryMetadata::apply(loaded,show,episode,metadata));
  metadata["show"]["uri"]=show;loaded.remember(episode);
  assert(CardLibraryMetadata::apply(loaded,episode,episode,metadata)); // Explicit episode keeps its own title.
  assert(!deserializeJson(list,loaded.libraryJson()));for(JsonObject card:list["cards"].as<JsonArray>())if(card["uri"]==episode)assert(card["title"]=="Episode title");
  loaded.label(album,"Old artist","Old album");loaded.started(album,"spotify:album:ABCDEFGHIJKLMNOPQRSTUV","",0);
  assert(!loaded.find(album)->title[0]);assert(loaded.save(libraryStore));PlaybackBookmarks switched;switched.load(libraryStore);assert(!switched.find(album)->title[0]);
  std::cout<<"Bookmark persistence, provenance, bounded storage and repeat-scan gesture tests passed\n";
}
