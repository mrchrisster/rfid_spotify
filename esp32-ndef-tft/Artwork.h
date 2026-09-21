#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
namespace Artwork {
// Use the requested album/track rather than racing Connect's playback state.
inline String metadataUrl(const String& uri) {
  if(uri.startsWith("spotify:album:")) return String("https://api.spotify.com/v1/albums/")+String(uri.c_str()+14);
  if(uri.startsWith("spotify:track:")) return String("https://api.spotify.com/v1/tracks/")+String(uri.c_str()+14);
  return "";
}
inline bool matchesPlayback(JsonDocument& doc,const String& uri) {
  if(uri.startsWith("spotify:album:"))return doc["item"]["album"]["uri"].as<String>()==uri;
  if(uri.startsWith("spotify:track:"))return doc["item"]["uri"].as<String>()==uri;
  return true;
}
inline String imageUrl(JsonDocument& doc,int maxWidth=640,int* chosenWidth=nullptr) {
  JsonArray images=doc["images"].as<JsonArray>();
  if(images.isNull())images=doc["album"]["images"].as<JsonArray>();
  if(images.isNull())images=doc["item"]["album"]["images"].as<JsonArray>();
  if(images.isNull())images=doc["item"]["images"].as<JsonArray>();
  String selected;int selectedWidth=0;
  for(JsonObject item:images) {
    if(!item["url"].is<const char*>() || !item["width"].is<int>())continue;
    String url=item["url"].as<String>();int width=item["width"].as<int>();
    if(width<240 || width>maxWidth || url.length()>1024 || !url.startsWith("https://i.scdn.co/"))continue;
    bool better=selected.isEmpty() || (width>=240 && (selectedWidth<240 || width<selectedWidth)) ||
                (width<240 && selectedWidth<240 && width>selectedWidth);
    if(better){selected=url;selectedWidth=width;}
  }
  if(chosenWidth)*chosenWidth=selectedWidth;
  return selected;
}
}
