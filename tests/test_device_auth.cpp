#include "DeviceAuth.cpp"
#include <WiFi.h>
#include <assert.h>
#include <stdio.h>
// HTTPS routes are exercised separately from identity crypto/storage tests.
namespace DeviceIdentity {
inline bool renew=false; inline int activations=0;
void begin() {} bool maintain() { bool changed=renew;renew=false;return changed; } void activate() { ++activations; }
const char* hostname() { return "spotify-player"; }
const char* key() { return "test-key"; } const char* chain() { return "test-chain"; }
int certificateDays() { return 397; }
}
uint32_t testMillis=1;
FakeWiFi WiFi;
std::deque<FakeResponse> responses;
std::vector<FakeRequest> requests;
extern const uint8_t bundleStart[] asm("_binary_x509_crt_bundle_start")={1};
extern const uint8_t bundleEnd[] asm("_binary_x509_crt_bundle_end")={0};
void logMessage(const String&){}
String saved;bool savedPkce=false;
bool save(const String& value,bool pkce){saved=value;savedPkce=pkce;return true;}
httpd_req_t request(bool authenticated=bool(PLAYER_REQUIRE_WEB_AUTH)){httpd_req_t r;if(authenticated)r.headers["Authorization"]="Basic encoded-credentials";return r;}
std::string cookie;
void start(){
 DeviceAuth::tick(true);
 auto r=request();DeviceAuth::home(&r);assert(r.status=="200 OK");
 cookie=r.responseHeaders["Set-Cookie"].substr(0,56);
 auto post=request();post.headers["Cookie"]=cookie;post.headers["Origin"]=DeviceAuth::origin.c_str();
 post.body="csrf="+std::string(DeviceAuth::session.browser);post.content_len=post.body.size();
 DeviceAuth::connect(&post);assert(post.status=="303 See Other");
 assert(post.responseHeaders["Location"].find("code_challenge_method=S256")!=std::string::npos);
 assert(DeviceAuth::session.active);
}
httpd_req_t callbackRequest(){auto r=request(false);r.headers["Cookie"]=cookie;r.query="code=a%2Bb&state="+std::string(DeviceAuth::session.state);return r;}
void releaseHttps(){
 auto waiting=request(false);waiting.headers["Cookie"]=cookie;
 DeviceAuth::outcome(&waiting);
 assert(waiting.responseHeaders["X-Reconnect-Pending"]=="1");
 assert(waiting.output.find("catch(e)")!=std::string::npos);
 DeviceAuth::tick(true);assert(DeviceAuth::exchangeReady && !DeviceAuth::ready());
}
int main(){
 DeviceAuth::begin("id","password");
 auto anonymous=request(false);DeviceAuth::home(&anonymous);assert(anonymous.status==(PLAYER_REQUIRE_WEB_AUTH ? "401 Unauthorized" : "200 OK"));
 auto home=request();DeviceAuth::home(&home);
 assert(home.responseHeaders["Referrer-Policy"]=="strict-origin");
 cookie=home.responseHeaders["Set-Cookie"].substr(0,56);
 assert(home.responseHeaders["Set-Cookie"].find("Secure; HttpOnly; SameSite=Lax")!=std::string::npos);
 auto cross=request();cross.headers["Cookie"]=cookie;cross.headers["Origin"]="https://evil.example";
 cross.body="csrf="+std::string(DeviceAuth::session.browser);cross.content_len=cross.body.size();
 DeviceAuth::connect(&cross);assert(cross.status=="400" && !DeviceAuth::session.active);
 auto nullOrigin=cross;nullOrigin.headers["Origin"]="null";
 DeviceAuth::connect(&nullOrigin);assert(nullOrigin.status=="400" && nullOrigin.output.find("origin did not match")!=std::string::npos);
 auto missingCookie=cross;missingCookie.headers.erase("Cookie");
 DeviceAuth::connect(&missingCookie);assert(missingCookie.status=="400" && missingCookie.output.find("Browser session missing")!=std::string::npos);
 cross.headers["Origin"]=DeviceAuth::origin.c_str();cross.body="csrf=wrong";cross.content_len=cross.body.size();
 DeviceAuth::connect(&cross);assert(!DeviceAuth::session.active);
 start();
 auto wrong=callbackRequest();wrong.headers.erase("Cookie");DeviceAuth::callback(&wrong);assert(wrong.status=="400");
 wrong=callbackRequest();wrong.query+="&state=duplicate";DeviceAuth::callback(&wrong);assert(wrong.status=="400");
 wrong=callbackRequest();wrong.query="code=abc&state=wrong";DeviceAuth::callback(&wrong);assert(wrong.status=="400");
 auto success=callbackRequest();auto replay=success;DeviceAuth::callback(&success);
 assert(success.status=="303 See Other" && success.responseHeaders["Location"]=="/result");
 assert(DeviceAuth::result.load()==202 && !DeviceAuth::session.active && !DeviceAuth::session.verifier[0]);
 DeviceAuth::callback(&replay);assert(replay.status=="400");
 SpotifyClient spotify("id","secret","Echo","old");spotify.SetRefreshTokenCallback(save);
 responses.push_back({200,"{\"access_token\":\"access\",\"refresh_token\":\"from-browser\",\"expires_in\":3600,\"token_type\":\"Bearer\"}"});
 size_t before=requests.size();assert(!DeviceAuth::process(spotify)&&requests.size()==before);
 // Failed HTTPS shutdown must never allow simultaneous outbound TLS.
 auto waiting=request(false);waiting.headers["Cookie"]=cookie;DeviceAuth::outcome(&waiting);
 fakeStopFailure=true;DeviceAuth::tick(true);assert(!DeviceAuth::exchangeReady);
 assert(!DeviceAuth::process(spotify)&&requests.size()==before);fakeStopFailure=false;
 releaseHttps();
 assert(DeviceAuth::process(spotify) && saved=="from-browser" && savedPkce && DeviceAuth::result.load()==200);
 assert(requests.back().body.indexOf("code=a%2Bb")>=0);
 auto result=request(false);result.headers["Cookie"]=cookie;DeviceAuth::outcome(&result);assert(result.output.find("reconnected and saved")!=std::string::npos);
 auto outsider=request(false);DeviceAuth::outcome(&outsider);assert(outsider.status=="403");
 start();auto expired=callbackRequest();testMillis+=300000;DeviceAuth::callback(&expired);assert(expired.status=="400");
 start();auto denied=callbackRequest();denied.query="error=access_denied&state="+std::string(DeviceAuth::session.state);
 DeviceAuth::callback(&denied);assert(DeviceAuth::result.load()==403 && !DeviceAuth::session.active && !DeviceAuth::process(spotify));
 assert(spotify.IsTokenValid() && saved=="from-browser");
 // A callback arriving during a shared local cooldown must remain queued.
 start();auto delayed=callbackRequest();DeviceAuth::callback(&delayed);releaseHttps();
 responses.push_back({500,"{}"});assert(spotify.CallAPI("GET","https://api.spotify.com/v1/me/player").httpCode==500);
 size_t calls=requests.size();assert(!DeviceAuth::process(spotify)&&DeviceAuth::busy()&&requests.size()==calls);
 assert(saved=="from-browser"&&spotify.IsTokenValid());
 testMillis+=5000;responses.push_back({-1,""});assert(!DeviceAuth::process(spotify)&&DeviceAuth::busy());
 calls=requests.size();assert(!DeviceAuth::process(spotify)&&requests.size()==calls);
 testMillis+=5000;responses.push_back({200,R"({"access_token":"new","refresh_token":"from-browser","expires_in":3600,"token_type":"Bearer"})"});
 assert(DeviceAuth::process(spotify)&&DeviceAuth::result.load()==200&&!DeviceAuth::busy()&&DeviceAuth::pending->entries.empty());
 // Real429 waits Retry-After, then exchanges the same code instead of discarding it.
 start();auto limited=callbackRequest();DeviceAuth::callback(&limited);releaseHttps();
 responses.push_back({429,"{}",-1,"1"});assert(!DeviceAuth::process(spotify));
 calls=requests.size();assert(!DeviceAuth::process(spotify)&&requests.size()==calls);
 testMillis+=1000;responses.push_back({200,R"({"access_token":"new","refresh_token":"from-browser","expires_in":3600,"token_type":"Bearer"})"});
 assert(DeviceAuth::process(spotify)&&DeviceAuth::result.load()==200);
 // Bound retries; old usable credentials survive all connection failures.
 start();auto failed=callbackRequest();DeviceAuth::callback(&failed);releaseHttps();
 for(int i=0;i<3;++i){responses.push_back({-1,""});assert(DeviceAuth::process(spotify)==(i==2));if(i<2)testMillis+=5000;}
 assert(DeviceAuth::result.load()==-1&&!DeviceAuth::busy()&&DeviceAuth::pending->entries.empty()&&spotify.IsTokenValid());testMillis+=5000;
 // Never retry a rejected or ambiguously consumed one-time authorization code.
 for(int error:{400,-11}){start();auto bad=callbackRequest();DeviceAuth::callback(&bad);releaseHttps();responses.push_back({error,"{}"});
 assert(DeviceAuth::process(spotify)&&DeviceAuth::result.load()==error&&!DeviceAuth::busy());if(error<0)testMillis+=5000;}
 start();auto timeout=callbackRequest();DeviceAuth::callback(&timeout);releaseHttps();
 responses.push_back({429,"{}",-1,"120"});assert(spotify.CallAPI("GET","https://api.spotify.com/v1/me/player").httpCode==429);
 calls=requests.size();assert(!DeviceAuth::process(spotify));testMillis+=90000;
 assert(DeviceAuth::process(spotify)&&DeviceAuth::result.load()==504&&requests.size()==calls&&DeviceAuth::pending->entries.empty());testMillis+=30000;
 assert(spotify.IsTokenValid()&&saved=="from-browser");
 DeviceAuth::tick(true); assert(DeviceAuth::ready() && !DeviceAuth::exchangeReady);
 DeviceIdentity::renew=true; fakeStopFailure=true;
 DeviceAuth::tick(true); assert(DeviceAuth::ready() && DeviceIdentity::activations==0);
 fakeStopFailure=false; testMillis+=60000;
 DeviceAuth::tick(true); assert(DeviceAuth::ready() && DeviceIdentity::activations==1);
 // Every actual registered route closes TLS after sending its full response,
 // including error paths. Handler business logic tests above remain unchanged.
 for(const char* path:{"/","/connect","/callback","/result"}){
   auto r=request();assert(fakeRoutes.at(path)(&r)==ESP_FAIL);
   assert(r.responseHeaders["Connection"]=="close");assert(!r.output.empty());
 }
 auto successPage=request(false);successPage.headers["Cookie"]=cookie;
 // Home above creates a fresh cookie: use it for a successful result response.
 successPage.headers["Cookie"]="__Host-rfid="+std::string(DeviceAuth::session.browser);
 DeviceAuth::result=200;
 assert(fakeRoutes.at("/result")(&successPage)==ESP_FAIL);
 assert(successPage.output.find("reconnected and saved")!=std::string::npos);
 assert(responses.empty());delete DeviceAuth::pending;
 puts("Production HTTPS handler / worker reconnect tests passed");
}
