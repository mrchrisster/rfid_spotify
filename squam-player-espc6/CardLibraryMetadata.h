#pragma once
#include "PlaybackBookmarks.h"
#include "Artwork.h"
// Reuse metadata already fetched for artwork. Never label a card with stale
// Connect state from a different album/episode/playlist after a switch.
namespace CardLibraryMetadata {
inline bool apply(PlaybackBookmarks& books,const String& card,const String& context,JsonDocument& doc){
  bool playback=!doc["item"].isNull();
  if(playback && (!Artwork::matchesPlayback(doc,context) ||
      (context.startsWith("spotify:playlist:") && doc["context"]["uri"].as<String>()!=context)))return false;
  JsonVariant item=playback?doc["item"].as<JsonVariant>():doc.as<JsonVariant>();
  // A show card represents the series, not its selected episode (e.g. "Vorspann").
  if(card.startsWith("spotify:show:")){
    const char* title=item["show"]["name"]|"";
    const char* showUri=item["show"]["uri"]|"";
    if(!*title || (*showUri && card!=showUri) || !books.contains(card.c_str()))return false;
    books.label(card.c_str(),item["show"]["publisher"]|"",title);return true;
  }
  const char* title=item["album"]["name"].as<const char*>();
  if(!title)title=item["name"]|"";
  const char* artist=item["album"]["artists"][0]["name"].as<const char*>();
  if(!artist)artist=item["artists"][0]["name"].as<const char*>();
  if(!artist)artist=item["show"]["publisher"]|"";
  if(!*title || !books.contains(card.c_str()))return false;
  books.label(card.c_str(),artist,title);return true;
}
}
