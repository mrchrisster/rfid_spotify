#include "WifiSetup.h"
#include "WifiSetupPolicy.h"
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <esp_system.h>
#include <atomic>

void logMessage(const String&);
namespace WifiSetup {
namespace {
Preferences storage;
DNSServer dns;
bool storageReady=false, pendingStart=false, pendingTrial=false, pendingCancel=false, trialActive=false;
std::atomic<bool> portalActive{false};
std::atomic<State> current{Closed};
char apName[32]={}, apPassword[17]={}; // Immutable after begin(), safe for TFT reads.
String savedSsid,savedPassword,candidateSsid,candidatePassword;
uint32_t disconnectedAt=0,retryAt=0,savedAt=0,closeAt=0;
bool closing=false, automaticPortal=false, restoredObserved=false;
uint32_t restoredAt=0;
WifiSetupPolicy::Trial trial;
const IPAddress portalIp(192,168,4,1);
void connectSaved() {
  WiFi.disconnect(false,false);
  if(!savedSsid.isEmpty()) WiFi.begin(savedSsid.c_str(),savedPassword.c_str());
  retryAt=millis();
}
void closePortal() {
  dns.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA);
  portalActive=false;current=Closed;closing=false;automaticPortal=false;restoredObserved=false;
  logMessage("[WiFi] Setup network closed");
}
void openPortal(bool automatic) {
  if(portalActive) { if(!automatic) automaticPortal=false; return; }
  WiFi.mode(WIFI_AP_STA);
  if(!WiFi.softAPConfig(portalIp,portalIp,IPAddress(255,255,255,0)) || !WiFi.softAP(apName,apPassword,1,false,2)) {
    logMessage("[WiFi] Could not start setup network; will retry");
    WiFi.softAPdisconnect(true);WiFi.mode(WIFI_STA);disconnectedAt=millis();return;
  }
  automaticPortal=automatic;restoredObserved=false;portalActive=true;current=Portal;
  if(!dns.start(53,"*",portalIp)) logMessage("[WiFi] Automatic setup-page discovery unavailable; use http://192.168.4.1");
  logMessage("[WiFi] Setup network: " + String(apName) + "; open http://192.168.4.1");
}
bool apRequest(WebServer& web) {
  return portalActive && web.client().localIP()==portalIp;
}
bool permit(WebServer& web, bool mutation=false) {
  if(!apRequest(web)) { web.send(403,"application/json","{\"message\":\"Join the player's setup Wi-Fi first\"}");return false; }
  if(mutation && web.header("X-Requested-With")!="RFIDPlayer") {
    web.send(403,"application/json","{\"message\":\"Missing request header\"}");return false;
  }
  web.sendHeader("Cache-Control","no-store");return true;
}
const char portalPage[] PROGMEM=R"HTML(<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>StoryPlayer Wi-Fi</title>
<style>body{font:18px system-ui;max-width:520px;margin:30px auto;padding:20px;background:#101c21;color:white}input,select,button{font:inherit;width:100%;box-sizing:border-box;padding:12px;margin:8px 0}button{background:#4ed2b8;border:0;border-radius:8px}#result{white-space:pre-wrap}small{display:block}</style></head><body>
<h1>StoryPlayer Wi-Fi</h1><select id="lang" aria-label="Language"><option value="de">Deutsch</option><option value="en">English</option><option value="fr">Français</option><option value="es">Español</option></select>
<p id="intro"></p><button id="scan"></button><select id="networks" aria-label="Wi-Fi networks"><option value="">—</option></select>
<form id="form"><label for="ssid" id="ssidLabel"></label><input id="ssid" maxlength="32" required autocomplete="off"><label for="password" id="passwordLabel"></label><input id="password" type="password" maxlength="64" autocomplete="new-password"><small id="openHint"></small><button id="save"></button></form>
<p id="result" role="status"></p><p><a id="chooseSpeaker" hidden style="color:#4ed2b8"></a></p><button id="cancel"></button><small id="note"></small>
<script>
const $=id=>document.getElementById(id),copy={
de:['Wähle dein WLAN oder gib den Namen ein.','Netzwerke suchen','WLAN-Name','WLAN-Passwort','Bei offenem WLAN leer lassen.','Verbinden und speichern','Abbrechen / altes WLAN','Bleibe verbunden, auch wenn dein Handy „Kein Internet“ meldet.','Bereit.','Verbindung wird geprüft…','WLAN gespeichert! Verbinde dein Handy wieder mit deinem WLAN.','Verbindung fehlgeschlagen. Alte Zugangsdaten bleiben erhalten.','Speichern fehlgeschlagen. Alte Zugangsdaten bleiben erhalten.','Setup-WLAN erneut verbinden und diese Seite öffnen.','Suche läuft…'],
en:['Choose your Wi-Fi or enter its name.','Find networks','Wi-Fi name','Wi-Fi password','Leave blank for an open network.','Connect and save','Cancel / previous Wi-Fi','Stay connected even if your phone says “No internet”.','Ready.','Testing connection…','Wi-Fi saved! Reconnect your phone to your home Wi-Fi.','Connection failed. Previous credentials are retained.','Saving failed. Previous credentials are retained.','Reconnect to the setup Wi-Fi and reopen this page.','Scanning…'],
fr:['Choisis ton Wi-Fi ou saisis son nom.','Chercher les réseaux','Nom du Wi-Fi','Mot de passe Wi-Fi','Laisser vide pour un réseau ouvert.','Connecter et enregistrer','Annuler / ancien Wi-Fi','Reste connecté même si le téléphone indique « Pas d’Internet ».','Prêt.','Connexion en cours…','Wi-Fi enregistré ! Reconnecte ton téléphone à ton Wi-Fi.','Échec de connexion. Anciens identifiants conservés.','Échec de sauvegarde. Anciens identifiants conservés.','Reconnecte-toi au Wi-Fi de configuration et ouvre cette page.','Recherche…'],
es:['Elige tu Wi-Fi o escribe su nombre.','Buscar redes','Nombre del Wi-Fi','Contraseña Wi-Fi','Dejar vacío para una red abierta.','Conectar y guardar','Cancelar / Wi-Fi anterior','Sigue conectado aunque el móvil indique «Sin Internet».','Listo.','Probando conexión…','¡Wi-Fi guardado! Vuelve a conectar el móvil a tu Wi-Fi.','Error de conexión. Se conservan los datos anteriores.','Error al guardar. Se conservan los datos anteriores.','Vuelve a conectar al Wi-Fi de configuración y abre esta página.','Buscando…']};
let testing=false,scanning=false,state=1,userLanguage=false;
const speakerStep={de:'Nächster Schritt: Heim-WLAN verbinden, dann Standard-Echo wählen',en:'Next: reconnect to home Wi-Fi, then choose the default Echo',fr:'Ensuite : rejoins ton Wi-Fi, puis choisis l’Echo par défaut',es:'Después: vuelve a tu Wi-Fi y elige el Echo predeterminado'};
function t(){return copy[$('lang').value]||copy.de;}
function labels(){$('chooseSpeaker').textContent=speakerStep[$('lang').value]||speakerStep.de;['intro','scan','ssidLabel','passwordLabel','openHint','save','cancel','note'].forEach((id,i)=>$(id).textContent=t()[i]);}
$('lang').onchange=()=>{userLanguage=true;labels();};labels();
async function request(path,body){let r=await fetch(path,body===undefined?{}:{method:'POST',headers:{'Content-Type':'application/json','X-Requested-With':'RFIDPlayer'},body:JSON.stringify(body)});let d=await r.json();if(!r.ok)throw Error(d.message||r.status);return d;}
$('networks').onchange=()=>{$('ssid').value=$('networks').value;};
$('scan').onclick=async()=>{try{await request('/wifi/scan',{});scanning=true;$('result').textContent=t()[14];}catch(e){$('result').textContent=e.message;}};
$('form').onsubmit=async e=>{e.preventDefault();if(testing)return;try{await request('/wifi/connect',{ssid:$('ssid').value,password:$('password').value});$('password').value='';testing=true;$('save').disabled=true;$('result').textContent=t()[9];}catch(e){$('result').textContent=e.message;}};
$('cancel').onclick=async()=>{try{await request('/wifi/cancel',{});$('result').textContent=t()[13];}catch(e){$('result').textContent=e.message;}};
async function poll(){try{let d=await request('/wifi/status');if(!userLanguage&&d.language){$('lang').value=d.language;labels();}state=d.state;$('chooseSpeaker').hidden=state!==3;$('chooseSpeaker').href=state===3?'http://'+d.ip+'/?setup=speaker#speakerSetup':'';testing=state===2;$('save').disabled=testing||state===3;$('scan').disabled=testing||state===3;$('cancel').disabled=state===3;$('result').textContent=t()[state===2?9:state===3?10:state===4?11:state===5?12:8]+(state===3?'\nhttp://'+d.ip+'/':'');if(scanning){let list=await request('/wifi/networks');if(!list.scanning){scanning=false;$('networks').replaceChildren(new Option('—',''));for(let n of list.networks)$('networks').append(new Option(n.ssid+' ('+n.rssi+' dBm)',n.ssid));}}}catch(e){$('result').textContent=t()[13];}if(state!==3)setTimeout(poll,2000);}poll();
</script></body></html>)HTML";
}

void begin(const char* fallbackSsid,const char* fallbackPassword) {
  storageReady=storage.begin("wifi_setup",false);
  savedSsid=fallbackSsid;savedPassword=fallbackPassword;
  if(storageReady) {
    String record=storage.getString("credentials","");JsonDocument doc;
    if(!record.isEmpty() && !deserializeJson(doc,record) && doc["ssid"].is<String>() && doc["password"].is<String>()) {
      String name=doc["ssid"].as<String>(),password=doc["password"].as<String>();
      if(WifiSetupPolicy::credentials(name.c_str(),name.length(),password.c_str(),password.length())) { savedSsid=name;savedPassword=password; }
    }
  }
  uint64_t mac=ESP.getEfuseMac();
  snprintf(apName,sizeof(apName),"StoryPlayer-Setup-%06lX",(unsigned long)(mac&0xffffff));
  String password=storageReady?storage.getString("setup_key",""):"";
  if(password.length()!=16) {
    char generated[17];snprintf(generated,sizeof(generated),"%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random());password=generated;
    if(!storageReady || storage.putString("setup_key",password)!=password.length()) Serial.println("WARNING: setup password is temporary; Wi-Fi settings storage unavailable");
  }
  strlcpy(apPassword,password.c_str(),sizeof(apPassword));
  // USB only: never put the setup password in shared diagnostic logs.
  Serial.printf("Wi-Fi setup: %s; password: %s; http://192.168.4.1\n",apName,apPassword);
  WiFi.persistent(false);WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(false);
  connectSaved();disconnectedAt=millis();
}
void requestStart() { pendingStart=true; }
bool active() { return portalActive.load(); }
State state() { return current.load(); }
const char* networkName() { return apName; }
const char* setupPassword() { return apPassword; }
bool servePortal(WebServer& web) {
  if(!apRequest(web))return false;
  web.sendHeader("Cache-Control","no-store");
  web.send_P(200,"text/html; charset=utf-8",portalPage);return true;
}
void tick() {
  uint32_t now=millis();
  if(pendingStart) {pendingStart=false;closing=false;openPortal(false);}
  if(pendingCancel) {pendingCancel=false;connectSaved();}
  if(pendingTrial) {
    pendingTrial=false;trialActive=true;current=Testing;trial.begin(now);
    WiFi.scanDelete();WiFi.disconnect(false,false);WiFi.begin(candidateSsid.c_str(),candidatePassword.c_str());retryAt=now;
  }
  bool connected=WiFi.status()==WL_CONNECTED && uint32_t(WiFi.localIP())!=0;
  if(trialActive) {
    bool matching=connected && WiFi.SSID()==candidateSsid;
    if(trial.ready(now,matching)) {
      JsonDocument doc;doc["ssid"]=candidateSsid;doc["password"]=candidatePassword;String record;serializeJson(doc,record);
      bool saved=storageReady && storage.putString("credentials",record)==record.length();
      trialActive=false;
      if(saved) {
        savedSsid=candidateSsid;savedPassword=candidatePassword;current=Saved;savedAt=now;
        logMessage("[WiFi] New network connected and credentials saved");
      } else {current=StorageFailed;connectSaved();logMessage("[WiFi] Saving credentials failed; previous network retained");}
      candidateSsid="";candidatePassword="";
    } else if(trial.expired(now)) {
      trialActive=false;current=Failed;candidateSsid="";candidatePassword="";connectSaved();
      logMessage("[WiFi] New network did not connect; previous credentials retained");
    }
  } else if(!connected && uint32_t(now-retryAt)>=15000 && !closing) connectSaved();
  if(connected) disconnectedAt=now;
  if(!portalActive && !connected && uint32_t(now-disconnectedAt)>=30000) openPortal(true);
  if(portalActive && automaticPortal && !trialActive && connected && WiFi.SSID()==savedSsid) {
    if(!restoredObserved) {restoredObserved=true;restoredAt=now;}
    if(uint32_t(now-restoredAt)>=10000) {
      logMessage("[WiFi] Saved network stable; closing automatic setup");closePortal();
    }
  } else restoredObserved=false;
  if(portalActive && current==Saved && uint32_t(now-savedAt)>=30000) closePortal();
  if(closing && uint32_t(now-closeAt)>=1000) closePortal();
}
void routes(WebServer& web) {
  web.on("/wifi",HTTP_GET,[&web]{if(!servePortal(web))web.send(403,"text/plain","Join setup Wi-Fi first");});
  web.on("/wifi/status",HTTP_GET,[&web]{
    if(!permit(web))return;JsonDocument doc;doc["state"]=int(state());doc["ip"]=WiFi.localIP().toString();
    extern const char* languageCode();doc["language"]=languageCode();
    String out;serializeJson(doc,out);web.send(200,"application/json",out);
  });
  web.on("/wifi/connect",HTTP_POST,[&web]{
    if(!permit(web,true))return;
    if(trialActive||pendingTrial||current==Saved) {web.send(409,"application/json","{\"message\":\"Connection update already in progress\"}");return;}
    String body=web.arg("plain");JsonDocument doc;
    if(body.length()>512 || deserializeJson(doc,body) || !doc["ssid"].is<String>() || !doc["password"].is<String>()) {web.send(400,"application/json","{\"message\":\"Invalid Wi-Fi request\"}");return;}
    String name=doc["ssid"].as<String>(),password=doc["password"].as<String>();
    if(!WifiSetupPolicy::credentials(name.c_str(),name.length(),password.c_str(),password.length())) {web.send(400,"application/json","{\"message\":\"SSID: 1-32 bytes; password: empty, 8-63 characters, or 64 hex digits\"}");return;}
    if(!storageReady) {web.send(507,"application/json","{\"message\":\"Settings storage unavailable\"}");return;}
    automaticPortal=false;candidateSsid=name;candidatePassword=password;pendingTrial=true;closing=false;
    web.send(202,"application/json","{\"message\":\"Testing connection\"}");
  });
  web.on("/wifi/cancel",HTTP_POST,[&web]{
    if(!permit(web,true))return;
    pendingTrial=false;trialActive=false;candidateSsid="";candidatePassword="";closing=true;closeAt=millis();current=Portal;
    pendingCancel=true;web.send(200,"application/json","{\"message\":\"Returning to saved network\"}");
  });
  web.on("/wifi/scan",HTTP_POST,[&web]{
    if(!permit(web,true))return;
    if(trialActive||pendingTrial||current==Saved) {web.send(409,"application/json","{\"message\":\"Wait for the connection test\"}");return;}
    automaticPortal=false; // Explicit setup activity must not close underneath the user.
    if(WiFi.scanComplete()!=WIFI_SCAN_RUNNING) {WiFi.scanDelete();WiFi.scanNetworks(true);}
    web.send(202,"application/json","{\"message\":\"Scanning\"}");
  });
  web.on("/wifi/networks",HTTP_GET,[&web]{
    if(!permit(web))return;int count=WiFi.scanComplete();JsonDocument doc;doc["scanning"]=count==WIFI_SCAN_RUNNING;
    JsonArray networks=doc["networks"].to<JsonArray>();
    for(int i=0;i<count && i<30;++i) {JsonObject n=networks.add<JsonObject>();n["ssid"]=WiFi.SSID(i);n["rssi"]=WiFi.RSSI(i);}
    String out;serializeJson(doc,out);web.send(200,"application/json",out);
  });
}
}
