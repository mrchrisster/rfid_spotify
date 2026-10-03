#pragma once
#include "SpotifyClient.h"
#include "SafeNdef.h"
#include <ArduinoJson.h>
// Network-worker owned plan. Build completely before playback; never append to
// Spotify's shared queue (ambiguous retries could duplicate parts or other books).
class ShowPlayback {
  String body;
public:
  static constexpr unsigned PageSize=5, MaxEpisodes=100;
  String first;
  unsigned count=0;
  void clear(){body=String();first=String();count=0;}
  int prepare(SpotifyClient& client,const String& show,const String& resume,uint32_t position){
    clear();char normalized[SafeNdef::MaxUri];
    if(!show.startsWith("spotify:show:") || !SafeNdef::normalize(show.c_str(),normalized,sizeof(normalized)) || show!=normalized || position>2147483647U)return 400;
    if(!resume.isEmpty() && (!resume.startsWith("spotify:episode:") || !SafeNdef::normalize(resume.c_str(),normalized,sizeof(normalized)) || resume!=normalized))return 400;
    String list="{\"uris\":[";bool found=resume.isEmpty();unsigned expectedTotal=0;
    const uint32_t started=millis();
    for(unsigned offset=0;offset<MaxEpisodes;offset+=PageSize){
      // Scoped response/doc release descriptions and images before next TLS call.
      HttpResult response=client.CallAPI("GET",String("https://api.spotify.com/v1/shows/")+String(show.c_str()+13)+"/episodes?limit="+String(PageSize)+"&offset="+String(offset));
      if(response.httpCode!=200){clear();return response.httpCode;}
      if(uint32_t(millis()-started)>45000){clear();return 504;}
      JsonDocument filter;filter["total"]=true;filter["next"]=true;filter["items"][0]["uri"]=true;filter["items"][0]["is_playable"]=true;
      JsonDocument doc;
      if(deserializeJson(doc,response.payload,DeserializationOption::Filter(filter)) || !doc["items"].is<JsonArray>() || !doc["total"].is<unsigned>()){clear();return 502;}
      unsigned total=doc["total"];
      if(total>MaxEpisodes){clear();return 413;} // Never truncate a book silently.
      if(offset==0)expectedTotal=total;
      if(total!=expectedTotal || offset>total){clear();return 502;}
      JsonArray items=doc["items"].as<JsonArray>();
      unsigned expected=total-offset<PageSize?total-offset:PageSize;
      if(items.size()!=expected){clear();return 502;}
      for(JsonObject item:items){
        String uri=item["uri"]|"";
        bool playable=item["is_playable"].is<bool>() && item["is_playable"].as<bool>();
        if(!uri.startsWith("spotify:episode:") || !SafeNdef::normalize(uri.c_str(),normalized,sizeof(normalized)) || uri!=normalized){clear();return 502;}
        if(!found && uri==resume){if(!playable){clear();return 422;}found=true;}
        if(!found || !playable)continue;
        // Canonical URIs contain no JSON metacharacters. Reject duplicate parts.
        String quoted=String("\"")+uri+"\"";
        if(list.indexOf(quoted.c_str())>=0){clear();return 502;}
        if(!count)first=uri;else list+=',';
        size_t previous=list.length();list+=quoted;
        if(list.length()!=previous+quoted.length()){clear();return 503;}
        ++count;
      }
      if(offset+items.size()==total){
        if(!doc["next"].isNull()){clear();return 502;}
        if(!found || !count){clear();return 422;}
        String suffix=String("],\"position_ms\":")+String(position)+"}";
        size_t previous=list.length();list+=suffix;
        if(list.length()!=previous+suffix.length()){clear();return 503;}
        body=list;if(body.length()!=list.length()){clear();return 503;}
        return 200;
      }
      if(!doc["next"].is<const char*>()){clear();return 502;}
    }
    clear();return 413;
  }
  int play(SpotifyClient& client){
    if(!count || body.isEmpty())return 400;
    if(client.DeviceId().isEmpty() && client.GetDevices().isEmpty())return client.LastError()==200?404:client.LastError();
    return client.CallAPI("PUT","https://api.spotify.com/v1/me/player/play?device_id="+client.DeviceId(),body).httpCode;
  }
};
