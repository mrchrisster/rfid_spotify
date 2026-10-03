#include "DeviceAuth.h"
#include "DeviceConfig.h"
#include "SpotifyClient.h"
#include "OAuthSession.h"
#include <esp_https_server.h>
#include <esp_random.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>
#include <base64.h>
#include <atomic>
#include <time.h>
#include "DeviceIdentity.h"

namespace DeviceAuth {
namespace {
httpd_handle_t server = nullptr;
QueueHandle_t pending = nullptr;
String appId, authorization, origin, redirectUri;
OAuth::Session session; // Only the HTTPS server task reads/writes browser session fields.
std::atomic<int> result{0}; // 0 idle, 202 exchanging, 200 saved; other values are error codes.
std::atomic<bool> running{false};
struct Exchange { char code[1025]; char verifier[45]; };
uint32_t lastStart = 0;
bool attemptedStart = false, reloadPending = false;
uint32_t reloadAttemptAt = 0; bool reloadAttempted = false;

bool randomUrl(char* out) {
  uint8_t bytes[32]; esp_fill_random(bytes, sizeof(bytes));
  size_t size = 0;
  if (mbedtls_base64_encode(reinterpret_cast<unsigned char*>(out), 45, &size, bytes, sizeof(bytes))) return false;
  while (size && out[size-1] == '=') --size;
  out[size] = 0;
  for (size_t i=0; i<size; ++i) { if (out[i]=='+') out[i]='-'; if (out[i]=='/') out[i]='_'; }
  return true;
}
String encode(const String& value) {
  const char* digits = "0123456789ABCDEF"; String out;
  for (size_t i=0; i<value.length(); ++i) {
    uint8_t c=value[i];
    if (isalnum(c) || c=='-' || c=='_' || c=='.' || c=='~') out+=char(c);
    else { out+='%'; out+=digits[c>>4]; out+=digits[c&15]; }
  }
  return out;
}
void headers(httpd_req_t* req) {
  httpd_resp_set_hdr(req,"Cache-Control","no-store");
  // no-referrer makes native form POSTs send Origin:null. strict-origin
  // preserves the origin check while never exposing callback paths/query codes.
  httpd_resp_set_hdr(req,"Referrer-Policy","strict-origin");
  httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
  httpd_resp_set_hdr(req,"X-Frame-Options","DENY");
  httpd_resp_set_hdr(req,"Content-Security-Policy","default-src 'none'; script-src 'unsafe-inline'; connect-src 'self'; form-action 'self' https://accounts.spotify.com; frame-ancestors 'none'; base-uri 'none'");
}
bool admin(httpd_req_t* req) {
#if PLAYER_REQUIRE_WEB_AUTH
  char value[192];
  if (httpd_req_get_hdr_value_str(req,"Authorization",value,sizeof(value)) == ESP_OK && OAuth::equal(value,authorization.c_str())) return true;
  httpd_resp_set_status(req,"401 Unauthorized");
  httpd_resp_set_hdr(req,"WWW-Authenticate","Basic realm=\"Spotify reconnect\", charset=\"UTF-8\"");
  httpd_resp_sendstr(req,"Sign in with your player's admin password."); return false;
#else
  (void)req; return true;
#endif
}
bool browser(httpd_req_t* req) {
  char cookies[256];
  if (!session.browser[0] || httpd_req_get_hdr_value_str(req,"Cookie",cookies,sizeof(cookies)) != ESP_OK) return false;
  // Cookie names are case sensitive. Reject duplicates, prefixes and extra suffixes.
  bool found=false;
  char* p=cookies;
  while (*p) {
    while (*p==' ') ++p;
    char* end=strchr(p,';'); if (end) *end=0;
    if (!strncmp(p,"__Host-rfid=",12)) {
      if (found || !OAuth::equal(p+12,session.browser)) return false;
      found=true;
    }
    if (!end) break;
    p=end+1;
  }
  return found;
}
esp_err_t go(httpd_req_t* req, const char* location) {
  httpd_resp_set_status(req,"303 See Other"); httpd_resp_set_hdr(req,"Location",location);
  return httpd_resp_sendstr(req,"");
}
esp_err_t home(httpd_req_t* req) {
  headers(req); if (!admin(req)) return ESP_OK;
  if (result.load()==202) return httpd_resp_sendstr(req,"A reconnect is being saved. Return to the previous result page, or retry shortly.");
  session.consume(); result=0;
  if (!randomUrl(session.browser)) return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Random generation failed");
  session.issued=millis();
  String cookie="__Host-rfid="+String(session.browser)+"; Path=/; Secure; HttpOnly; SameSite=Lax; Max-Age=600";
  httpd_resp_set_hdr(req,"Set-Cookie",cookie.c_str());
  httpd_resp_set_type(req,"text/html; charset=utf-8");
  String page="<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><title>Reconnect Spotify</title><h1>Reconnect Spotify</h1>"
    "<p>Sign in to Spotify to reconnect this player. Your existing connection stays available if login is cancelled.</p>"
    "<form action='/connect' method='post'><input type='hidden' name='csrf' value='"+String(session.browser)+
    "'><button>Continue to Spotify</button></form>";
  return httpd_resp_sendstr(req,page.c_str());
}
esp_err_t connect(httpd_req_t* req) {
  headers(req); if (!admin(req)) return ESP_OK;
  char suppliedOrigin[160], body[128], csrf[45];
  if (result.load()==202) return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"A reconnect is already being saved. Wait for its result.");
  if (!browser(req)) {
    logMessage("[Auth] Reconnect rejected: browser session cookie missing or mismatched");
    return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,
      "Browser session missing. Trust this device's CA certificate, allow site cookies, then reopen the reconnect link in one tab.");
  }
  if (session.active || uint32_t(millis()-session.issued)>=300000)
    return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"This login form expired or was already used. Reopen the reconnect page.");
  if (httpd_req_get_hdr_value_str(req,"Origin",suppliedOrigin,sizeof(suppliedOrigin))!=ESP_OK || origin!=suppliedOrigin) {
    logMessage("[Auth] Reconnect rejected: request origin missing or mismatched");
    return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,
      "Browser origin did not match. Reopen the HTTPS hostname shown in the dashboard (not its IP address). Close older reconnect tabs.");
  }
  if (req->content_len<=0 || req->content_len>=sizeof(body))
    return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid reconnect form length. Reload the page.");
  size_t read=0;
  while (read<req->content_len) {
    int n=httpd_req_recv(req,body+read,req->content_len-read);
    if (n<=0) return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Incomplete request");
    read+=n;
  }
  body[read]=0;
  if (!OAuth::parameter(body,"csrf",csrf,sizeof(csrf)) || !OAuth::equal(csrf,session.browser))
    return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid request");
  if (time(nullptr)<1700000000) {
    httpd_resp_set_status(req,"503 Service Unavailable");
    return httpd_resp_sendstr(req,"Waiting for network time. Retry shortly.");
  }
  if (!randomUrl(session.state) || !randomUrl(session.verifier)) return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Random generation failed");
  uint8_t digest[32]; char challenge[45]; size_t size=0;
  if (mbedtls_sha256(reinterpret_cast<const uint8_t*>(session.verifier),43,digest,0) ||
      mbedtls_base64_encode(reinterpret_cast<unsigned char*>(challenge),sizeof(challenge),&size,digest,sizeof(digest)))
    return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"PKCE generation failed");
  challenge[43]=0;
  for (int i=0;i<43;++i) { if(challenge[i]=='+')challenge[i]='-'; if(challenge[i]=='/')challenge[i]='_'; }
  session.issued=millis(); session.active=true;
  String location="https://accounts.spotify.com/authorize?response_type=code&client_id="+encode(appId)+
    "&redirect_uri="+encode(redirectUri)+"&scope=user-read-playback-state%20user-modify-playback-state%20user-read-currently-playing"
    "&code_challenge_method=S256&code_challenge="+String(challenge)+"&state="+String(session.state);
  return go(req,location.c_str());
}
esp_err_t callback(httpd_req_t* req) {
  headers(req);
  char query[2048], state[45], error[128]; Exchange exchange{};
  size_t length=httpd_req_get_url_query_len(req);
  if (!length || length>=sizeof(query) || httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK ||
      !OAuth::parameter(query,"state",state,sizeof(state)) || !browser(req) || !session.valid(state,session.browser,millis()))
    return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Expired or invalid login. Start again from the reconnect page.");
  if (OAuth::parameter(query,"error",error,sizeof(error))) {
    session.consume(); result=403; return go(req,"/result");
  }
  if (!OAuth::parameter(query,"code",exchange.code,sizeof(exchange.code)) || !exchange.code[0])
    return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Missing or malformed authorization code");
  memcpy(exchange.verifier,session.verifier,sizeof(exchange.verifier));
  session.consume(); result=202;
  if (xQueueSend(pending,&exchange,0)!=pdTRUE) result=503;
  memset(&exchange,0,sizeof(exchange)); memset(query,0,sizeof(query));
  return go(req,"/result");
}
esp_err_t outcome(httpd_req_t* req) {
  headers(req);
  if (!browser(req)) return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Open reconnect in the browser where you started login.");
  int code=result.load();
  httpd_resp_set_type(req,"text/html; charset=utf-8");
  const char* message=code==200 ? "Spotify reconnected and saved. You can close this page and scan a card." :
    code==202 ? "Saving your Spotify connection..." :
    code==507 ? "Spotify connected, but flash storage failed. Do not reboot. Check the dashboard before retrying." :
    "Reconnect did not complete. Your previous connection has been retained. Start again to retry.";
  String page="<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'><title>Spotify connection</title><h1>Spotify connection</h1><p>"+String(message)+"</p>";
  if (code==202) page+="<script>setTimeout(()=>location.replace('/result'),1500)</script>";
  else page+="<p><a href='/'>Reconnect again</a></p>";
  return httpd_resp_sendstr(req,page.c_str());
}
}
void begin(const String& id, const String& password) {
  DeviceIdentity::begin();
  appId=id;
#if PLAYER_REQUIRE_WEB_AUTH
  authorization="Basic "+base64::encode("admin:"+password);
#else
  (void)password;
#endif
  origin="https://"+String(DeviceIdentity::hostname())+".local"; redirectUri=origin+"/callback";
  pending=xQueueCreate(1,sizeof(Exchange));
}
void tick(bool connected) {
  if (!connected || !pending || !DeviceIdentity::chain()[0]) return;
  if (result.load()!=202 && DeviceIdentity::maintain()) reloadPending=true;
  if (reloadPending && result.load()!=202) {
    if (reloadAttempted && uint32_t(millis()-reloadAttemptAt)<60000) return;
    reloadAttempted=true; reloadAttemptAt=millis();
    logMessage("[HTTPS] Reloading certificate; HTTPS server restart only, device uptime " + String(millis()/1000) + "s");
    // Certificate config borrows PEM memory: stop all handlers before replacing it.
    running=false;
    if (server) {
      if (httpd_ssl_stop(server)!=ESP_OK) { running=true; logMessage("[HTTPS] Certificate reload delayed; retaining running server"); return; }
      server=nullptr;
    }
    DeviceIdentity::activate(); attemptedStart=false; reloadPending=false; reloadAttempted=false;
  }
  if (server) return;
  if (attemptedStart && uint32_t(millis()-lastStart)<60000) return;
  attemptedStart=true; lastStart=millis();
  httpd_ssl_config_t config=HTTPD_SSL_CONFIG_DEFAULT();
  config.httpd.task_priority=1; config.httpd.stack_size=8192;
  config.httpd.max_open_sockets=1; config.httpd.max_uri_handlers=4;
  config.httpd.max_uri_len=2048; config.httpd.max_req_hdr_len=2048;
  config.httpd.lru_purge_enable=true; config.httpd.recv_wait_timeout=5; config.httpd.send_wait_timeout=5;
  config.servercert=reinterpret_cast<const uint8_t*>(DeviceIdentity::chain()); config.servercert_len=strlen(DeviceIdentity::chain())+1;
  config.prvtkey_pem=reinterpret_cast<const uint8_t*>(DeviceIdentity::key()); config.prvtkey_len=strlen(DeviceIdentity::key())+1;
  config.tls_handshake_timeout_ms=8000;
  if (httpd_ssl_start(&server,&config)!=ESP_OK) { server=nullptr; logMessage("[HTTPS] Start failed; will retry"); return; }
  httpd_uri_t routes[4]{};
  routes[0].uri="/"; routes[0].method=HTTP_GET; routes[0].handler=home;
  routes[1].uri="/connect"; routes[1].method=HTTP_POST; routes[1].handler=connect;
  routes[2].uri="/callback"; routes[2].method=HTTP_GET; routes[2].handler=callback;
  routes[3].uri="/result"; routes[3].method=HTTP_GET; routes[3].handler=outcome;
  for (auto& route:routes) if (httpd_register_uri_handler(server,&route)!=ESP_OK) {
    httpd_ssl_stop(server); server=nullptr; logMessage("[HTTPS] Route registration failed"); return;
  }
  running=true; logMessage("[HTTPS] Reconnect available at "+origin);
}
bool process(SpotifyClient& spotify) {
  if (result.load() == 507 && !spotify.HasUnsavedToken()) result = 200;
  Exchange exchange{};
  if (!pending || xQueueReceive(pending,&exchange,0)!=pdTRUE) return false;
  bool success=spotify.AuthorizeCode(exchange.code,exchange.verifier,redirectUri);
  memset(&exchange,0,sizeof(exchange));
  result=success ? 200 : (spotify.LastError() == 202 ? 502 : spotify.LastError());
  logMessage(success ? "[Auth] Browser reconnect saved" : "[Auth] Browser reconnect failed (code "+String(result.load())+")");
  return true;
}
bool ready() { return running.load(); }
int certificateDays() { return DeviceIdentity::certificateDays(); }
const char* hostname() { return DeviceIdentity::hostname(); }
String url() { return origin+"/"; }
}
