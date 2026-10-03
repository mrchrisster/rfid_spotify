#pragma once
#include "SpotifyClient.h"
#include <ArduinoJson.h>
#include "Reliability.h"
#include "SafeNdef.h"
// Network-owned shuffled catalog offsets; a chosen URI is frozen by the playback job.
class ArtistAlbumSelection {
  SpotifyClient& spotify;uint32_t (*randomValue)();
  String artist;
  AlbumOrder order;
public:
  ArtistAlbumSelection(SpotifyClient& client,uint32_t (*random)()):spotify(client),randomValue(random){}
  int choose(const String& uri, String& album,const String& excluded="") {
    char normalized[SafeNdef::MaxUri];
    if(!uri.startsWith("spotify:artist:") || !SafeNdef::normalize(uri.c_str(),normalized,sizeof(normalized)))return 400;
    String requested = String(uri.c_str()+15);
    if (artist != requested || order.empty()) {
      HttpResult response = spotify.CallAPI("GET", "https://api.spotify.com/v1/artists/" + requested + "/albums?include_groups=album,single&limit=1");
      if (response.httpCode != 200) return response.httpCode;
      JsonDocument doc;
      if (deserializeJson(doc, response.payload) || !doc["total"].is<unsigned int>()) return 502;
      unsigned total = doc["total"].as<unsigned>();
      if (!total) return 404;
      if (total > 5000) return 413;
      unsigned count = total, start = 0;
      if ((requested == "1l6d0RIxTL3JytlLGvWzYe" || requested == "3t2iKODSDyzoDJw7AsD99u") && total > 60) { count = 60; start = total - 60; }
      std::vector<uint16_t> replacement(count);
      for (unsigned i = 0; i < count; ++i) replacement[i] = start + i;
      for (size_t i = count; i > 1; --i) std::swap(replacement[i - 1], replacement[randomValue() % i]);
      order.commit(replacement); artist = requested;
    }
    if (order.empty()) return 404;
    if (order.exhausted()) order.reshuffle(randomValue);
    // Bound requests when duplicate releases or a one-album catalog offer no alternative.
    for(size_t attempt=0;attempt<std::min(size_t(10),order.size());++attempt){
      uint16_t index;
      if (!order.current(index)) return 404;
      HttpResult response = spotify.CallAPI("GET", "https://api.spotify.com/v1/artists/" + requested + "/albums?include_groups=album,single&limit=1&offset=" + String(index));
      if (response.httpCode != 200) return response.httpCode;
      JsonDocument doc;
      if (deserializeJson(doc, response.payload)) return 502;
      String candidate = doc["items"][0]["uri"] | "";
      char checked[SafeNdef::MaxUri];
      if (!candidate.startsWith("spotify:album:") || !SafeNdef::normalize(candidate.c_str(), checked, sizeof(checked))) {
        // Catalog changed; rebuild on the next scan rather than staying stuck on a removed offset.
        order.clear(); artist = ""; return 404;
      }
      if(!excluded.isEmpty() && candidate==excluded){order.played();if(order.exhausted())order.rewind();continue;}
      album=candidate;return 200;
    }
    return 422;
  }
  int chooseForCard(const String& card,String& album,const String& excluded){
    if(card.startsWith("spotify:artist:"))return choose(card,album,excluded);
    char normalized[SafeNdef::MaxUri];
    if(!card.startsWith("spotify:album:") || !SafeNdef::normalize(card.c_str(),normalized,sizeof(normalized)))return 400;
    String track,primaryArtist;
    {
      // Album detail can exceed the24KB JSON cap on long audiobooks. A single
      // track's album object contains album artists without the huge track list.
      HttpResult response=spotify.CallAPI("GET",String("https://api.spotify.com/v1/albums/")+String(card.c_str()+14)+"/tracks?limit=1");
      if(response.httpCode!=200)return response.httpCode;
      JsonDocument doc;if(deserializeJson(doc,response.payload))return 502;
      track=doc["items"][0]["uri"]|"";
      if(!track.startsWith("spotify:track:") || !SafeNdef::normalize(track.c_str(),normalized,sizeof(normalized)))return 422;
    }
    {
      HttpResult response=spotify.CallAPI("GET",String("https://api.spotify.com/v1/tracks/")+String(track.c_str()+14));
      if(response.httpCode!=200)return response.httpCode;
      JsonDocument doc;if(deserializeJson(doc,response.payload))return 502;
      // Reject relinked/mismatched albums rather than silently changing artists.
      if(doc["album"]["uri"].as<String>()!=card)return 502;
      primaryArtist=doc["album"]["artists"][0]["uri"]|"";
      if(!primaryArtist.startsWith("spotify:artist:") || !SafeNdef::normalize(primaryArtist.c_str(),normalized,sizeof(normalized)))return 422;
    }
    return choose(primaryArtist,album,excluded);
  }
  void played() { order.played(); }
};

