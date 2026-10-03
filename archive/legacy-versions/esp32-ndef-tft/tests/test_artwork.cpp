#include <Arduino.h>
#include "Artwork.h"
#include <cassert>
#include <iostream>
int main() {
  assert(Artwork::metadataUrl("spotify:album:abc")=="https://api.spotify.com/v1/albums/abc");
  assert(Artwork::metadataUrl("spotify:track:def")=="https://api.spotify.com/v1/tracks/def");
  assert(Artwork::metadataUrl("spotify:playlist:abc").isEmpty());
  for(const char* shape:{"images","album","playback","episode"}) {
    JsonDocument d;JsonArray a;
    if(String(shape)=="images")a=d["images"].to<JsonArray>();
    else if(String(shape)=="album")a=d["album"]["images"].to<JsonArray>();
    else if(String(shape)=="playback")a=d["item"]["album"]["images"].to<JsonArray>();
    else a=d["item"]["images"].to<JsonArray>();
    for(int width:{64,640,300,200}) {
      JsonObject i=a.add<JsonObject>();i["width"]=width;i["url"]=String("https://i.scdn.co/")+String(width);
    }
    assert(Artwork::imageUrl(d)=="https://i.scdn.co/300");
    a.remove(2);a.remove(1);assert(Artwork::imageUrl(d).isEmpty());
    a[1]["url"]="https://untrusted.example/image";assert(Artwork::imageUrl(d).isEmpty());
    a[0]["width"]=-1;assert(Artwork::imageUrl(d).isEmpty());
  }
  JsonDocument sizes;
  deserializeJson(sizes,R"({"images":[{"width":640,"url":"https://i.scdn.co/large"},{"width":300,"url":"https://i.scdn.co/medium"},{"width":64,"url":"https://i.scdn.co/small"}]})");
  int width=0;
  assert(Artwork::imageUrl(sizes,640,&width)=="https://i.scdn.co/medium" && width==300);
  assert(Artwork::imageUrl(sizes,width-1,&width).isEmpty());
  JsonDocument playback;
  playback["item"]["album"]["uri"]="spotify:album:abc";
  assert(Artwork::matchesPlayback(playback,"spotify:album:abc"));
  assert(!Artwork::matchesPlayback(playback,"spotify:album:old"));
  assert(!Artwork::matchesPlayback(playback,"spotify:track:def"));
  playback["item"]["uri"]="spotify:track:def";
  assert(Artwork::matchesPlayback(playback,"spotify:track:def"));
  JsonDocument empty;assert(Artwork::imageUrl(empty).isEmpty());
  std::cout<<"Artwork metadata and image selection tests passed\n";
}
