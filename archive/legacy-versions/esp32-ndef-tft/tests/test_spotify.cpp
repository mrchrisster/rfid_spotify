#include "SpotifyClient.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <assert.h>
#include <stdio.h>
uint32_t testMillis=1;
FakeWiFi WiFi;
std::deque<FakeResponse> responses;
std::vector<FakeRequest> requests;
extern const uint8_t bundleStart[] asm("_binary_x509_crt_bundle_start")={1};
extern const uint8_t bundleEnd[] asm("_binary_x509_crt_bundle_end")={0};
bool savedPkce=false;bool saveOk=true;String saved;std::vector<String> logs;
void logMessage(const String& s){logs.push_back(s);}
bool save(const String& s,bool pkce){savedPkce=pkce;saved=s;return saveOk;}
void token(const char* access="access",const char* refresh="refresh"){
 String body=String("{\"access_token\":\"")+access+"\",\"refresh_token\":\""+refresh+"\",\"expires_in\":3600}";
 responses.push_back({200,body});
}
void init(SpotifyClient& client){client.SetRefreshTokenCallback(save);token();assert(client.EnsureTokenFresh());}
int main(){
 SpotifyClient client("id","secret","Echo A","bootstrap");init(client);
 assert(saved=="refresh"&&client.IsTokenValid());
 // The dashboard test must hit Spotify even when the access token is still valid.
 size_t refreshCalls=requests.size();token();assert(client.EnsureTokenFresh(true));
 assert(requests.size()==refreshCalls+1 && requests.back().body.indexOf("grant_type=refresh_token")>=0);
 // Empty playback mutations need explicit HTTP framing (regression for Spotify 411).
 for(const char* endpoint:{"pause?device_id=B","play?device_id=B","volume?volume_percent=60&device_id=B","shuffle?state=false&device_id=B"}){
  responses.push_back({204,"",0});
  assert(client.CallAPI("PUT",String("https://api.spotify.com/v1/me/player/")+endpoint).httpCode==204);
  assert(requests.back().contentLength=="0" && requests.back().body.isEmpty());
 }
 responses.push_back({204,"",0});
 assert(client.CallAPI("POST","https://api.spotify.com/v1/me/player/next?device_id=B").httpCode==204);
 assert(requests.back().contentLength=="0");
 responses.push_back({200,"{}"});assert(client.CallAPI("GET","https://api.spotify.com/v1/me/player").httpCode==200);
 assert(requests.back().contentLength.isEmpty());
 // Failed replacement must preserve both the valid access token and persisted refresh token.
 responses.push_back({400,"{\"error\":\"invalid_grant\"}"});
 assert(!client.ReplaceRefreshToken("bad")&&client.IsTokenValid()&&!client.IsRevoked()&&saved=="refresh");
 // Unknown-length/chunked bodies are decoded by HTTPClient and must not be discarded.
 responses.push_back({200,"{\"devices\":[]}",-1});
 assert(client.CallAPI("GET","https://api.spotify.com/v1/me/player/devices").payload=="{\"devices\":[]}");
 assert(requests.back().authorization=="Bearer access");
 // Device selection and track-vs-context body, including top-level position_ms.
 client.SelectDevice("Echo B","B");responses.push_back({204,"",0});
 assert(client.Play("spotify:track:0123456789ABCDEFGHIJKL")==204);
 assert(requests.back().url=="https://api.spotify.com/v1/me/player/play?device_id=B");
 assert(requests.back().contentLength==String(requests.back().body.length()) && requests.back().contentLength!="0");
 JsonDocument body;assert(!deserializeJson(body,requests.back().body));
 assert(body["uris"][0]=="spotify:track:0123456789ABCDEFGHIJKL"&&body["position_ms"]==0&&body["context_uri"].isNull());
 responses.push_back({204,"",0});assert(client.Play("spotify:album:0123456789ABCDEFGHIJKL")==204);
 assert(!deserializeJson(body,requests.back().body));assert(body["offset"]["position"]==0&&body["position_ms"]==0&&body["offset"]["position_ms"].isNull());
 // 404 invalidates the single device ID; next command rediscovers using the selected name.
 responses.push_back({404,"{}"});assert(client.Next()==404&&client.DeviceId().isEmpty());
 responses.push_back({200,"{\"devices\":[{\"id\":\"new-B\",\"name\":\"Echo B\",\"is_restricted\":false}]}"});
 responses.push_back({204,"",0});assert(client.Next()==204&&client.DeviceId()=="new-B");
 // Retry-After is shared by calls; no network request is made during cooldown.
 responses.push_back({429,"{}",-1,"45"});assert(client.Next()==429);size_t calls=requests.size();
 assert(client.Next()==429&&requests.size()==calls&&client.RetryInMs()==45000);
 testMillis+=45000;responses.push_back({204,"",0});assert(client.Next()==204);
 // Mid-call 401 refreshes once and retries with the new bearer token.
 responses.push_back({401,"{}"});token("new-access","new-refresh");responses.push_back({204,"",0});
 assert(client.Next()==204&&saved=="new-refresh"&&requests.back().authorization=="Bearer new-access");
 // Invalid token JSON must not authenticate or replace persisted credentials.
 responses.push_back({200,"{\"expires_in\":3600}"});assert(!client.ReplaceRefreshToken("candidate")&&saved=="new-refresh");
 // Failed NVS write is reported, retained in RAM, and retried even while access token remains fresh.
 saveOk=false;token("newer-access","newer-refresh");assert(!client.ReplaceRefreshToken("candidate")&&client.LastError()==507);
 saveOk=true;assert(client.EnsureTokenFresh()&&saved=="newer-refresh");
 // Oversized and truncated API bodies are failures, not apparent success.
 responses.push_back({200,"{}",30000});assert(client.CallAPI("GET","https://api.spotify.com/v1/me/player").httpCode==502);
 responses.push_back({200,"{}",9});assert(client.CallAPI("GET","https://api.spotify.com/v1/me/player").httpCode==502);
 // Decoded download supports unknown size, rejects partial and oversized data.
 uint8_t image[20];responses.push_back({200,"JPEG",-1});assert(client.DownloadFile("https://i.scdn.co/image/x",image,sizeof(image))==4);
 responses.push_back({200,"JPEG",10});assert(client.DownloadFile("https://i.scdn.co/image/x",image,sizeof(image))==0);
 responses.push_back({200,"123456789012345678901",-1});assert(client.DownloadFile("https://i.scdn.co/image/x",image,sizeof(image))==0);
 calls=requests.size();assert(client.DownloadFile("http://evil/image",image,sizeof(image))==0&&calls==requests.size());
 // Expired token + revoked refresh stops subsequent automatic retries until user intervention.
 testMillis+=3600000;responses.push_back({400,"{\"error\":\"invalid_grant\"}"});assert(!client.EnsureTokenFresh()&&client.IsRevoked());
 calls=requests.size();assert(!client.EnsureTokenFresh()&&requests.size()==calls);
 // Browser code exchange must save grant type, omit the client secret, and encode inputs.
 SpotifyClient login("id","secret","Echo A","old"); login.SetRefreshTokenCallback(save);
 const char* verifier="abcdefghijklmnopqrstuvwxyz0123456789ABCDEFG";
 const char* redirect="https://spotify-player.local/callback";
 responses.push_back({200,"{\"access_token\":\"browser-access\",\"refresh_token\":\"browser-refresh\",\"expires_in\":3600,\"token_type\":\"Bearer\",\"scope\":\"user-read-playback-state user-modify-playback-state\"}"});
 assert(login.AuthorizeCode("a+b/c=",verifier,redirect));
 assert(saved=="browser-refresh" && savedPkce && login.IsTokenValid());
 assert(requests.back().authorization.isEmpty());
 assert(requests.back().body.indexOf("code=a%2Bb%2Fc%3D")>=0);
 assert(requests.back().body.indexOf("code_verifier=")>=0 && requests.back().body.indexOf("secret")<0);
 // Simulate reboot: restored mode must use client_id (no Basic auth) and retain an omitted refresh token.
 SpotifyClient reboot("id","secret","Echo A",""); reboot.SetRefreshToken(saved,savedPkce); reboot.SetRefreshTokenCallback(save);
 responses.push_back({200,"{\"access_token\":\"refreshed-browser-access\",\"expires_in\":3600}"});
 assert(reboot.EnsureTokenFresh());
 assert(requests.back().authorization.isEmpty() && requests.back().body.indexOf("client_id=id")>=0);
 assert(saved=="browser-refresh" && savedPkce);
 // Invalid code and malformed success preserve the established connection.
 responses.push_back({400,"{\"error\":\"invalid_grant\"}"});
 assert(!reboot.AuthorizeCode("bad",verifier,redirect) && reboot.IsTokenValid() && !reboot.IsRevoked());
 responses.push_back({200,"{\"access_token\":\"new\",\"expires_in\":3600,\"token_type\":\"Bearer\"}"});
 assert(!reboot.AuthorizeCode("bad",verifier,redirect) && saved=="browser-refresh");
 responses.push_back({200,"{\"access_token\":\"new\",\"refresh_token\":\"unwanted\",\"expires_in\":3600,\"token_type\":\"Bearer\",\"scope\":\"user-read-email\"}"});
 assert(!reboot.AuthorizeCode("bad",verifier,redirect) && reboot.LastError()==403 && saved=="browser-refresh");
 // Token rotation + failed persistence keeps PKCE mode through the save retry.
 saveOk=false; token("pkce-access","pkce-rotated");
 assert(!reboot.EnsureTokenFresh(true) && reboot.HasUnsavedToken() && reboot.LastError()==507 && savedPkce);
 saveOk=true; assert(reboot.EnsureTokenFresh() && !reboot.HasUnsavedToken() && saved=="pkce-rotated" && savedPkce);
 // Manual legacy token replacement intentionally restores confidential-client refresh mode.
 token("legacy-access","legacy-refresh"); assert(reboot.ReplaceRefreshToken("manual") && !savedPkce);
 assert(!requests.back().authorization.isEmpty());
 for(auto& message:logs){assert(message.indexOf("access_token")<0&&message.indexOf("newer-refresh")<0);}
 assert(responses.empty());puts("Spotify transport/state regression tests passed");
}
