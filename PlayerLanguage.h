#pragma once
#include <stdint.h>
#include <string.h>
namespace PlayerLanguage {
constexpr uint8_t Count=4, Default=1; // German unless a saved choice overrides it.
inline const char* code(uint8_t id) { static const char* codes[]={"en","de","fr","es"}; return codes[id<Count?id:Default]; }
inline int parse(const char* value) {
  if(value) for(uint8_t i=0;i<Count;++i) if(strcmp(value,code(i))==0) return i;
  return -1;
}
struct Copy {
  const char *title[6], *hint[6], *wifi, *reader, *wait, *login, *noAddress;
};
inline const Copy& get(uint8_t id) {
  static const Copy translations[] = {
    {{"Finding Wi-Fi...","A little help?","One moment...","Finding stories...","Saving my settings","Pick a story!"},
     {"My stories need a connection","Ask a grown-up to connect Spotify","My card reader is waking up","Connecting to Spotify","Please keep me plugged in","Place it on my reader"},"Wi-Fi","Reader","...","LOGIN","Web address: waiting for Wi-Fi"},
    {{"Suche WLAN...","Hilfst du mir?","Einen Moment...","Suche Geschichten...","Ich speichere...","Wähle eine Geschichte!"},
     {"Meine Geschichten brauchen Internet","Bitte Erwachsene, Spotify zu verbinden","Mein Kartenleser wacht auf","Verbinde mit Spotify","Bitte lass mich eingeschaltet","Lege die Karte auf den Leser"},"WLAN","Leser","...","HILFE","Webadresse: warte auf WLAN"},
    {{"Connexion Wi-Fi...","Un peu d'aide ?","Un instant...","Place aux histoires !","Enregistrement...","Choisis une histoire !"},
     {"Mes histoires ont besoin d'Internet","Demande à un adulte de connecter Spotify","Mon lecteur de cartes se réveille","Connexion à Spotify","Laisse-moi allumé, s'il te plaît","Pose la carte sur le lecteur"},"Wi-Fi","Lecteur","...","AIDE","Adresse web : en attente du Wi-Fi"},
    {{"Buscando Wi-Fi...","¿Me ayudas?","Un momento...","Buscando historias...","Guardando ajustes...","¡Elige una historia!"},
     {"Mis historias necesitan Internet","Pide a un adulto que conecte Spotify","Mi lector de tarjetas se está despertando","Conectando con Spotify","Déjame encendido, por favor","Pon la tarjeta sobre el lector"},"Wi-Fi","Lector","...","AYUDA","Dirección web: esperando Wi-Fi"}
  };
  return translations[id<Count?id:Default];
}
// The built-in GFX font is CP437, not UTF-8. Convert our Latin accents to one
// glyph each so both rendering and centering use the correct character count.
inline uint8_t glyph(const char*& p) {
  uint8_t first=uint8_t(*p++);
  if(first<128) return first;
  if((first==0xc2 || first==0xc3) && *p) {
    uint16_t cp=((first&31)<<6)|(uint8_t(*p++)&63);
    switch(cp) {
      case 0xe4:return 132; case 0xf6:return 148; case 0xfc:return 129;
      case 0xdf:return 225; case 0xe9:return 130; case 0xe8:return 138;
      case 0xea:return 136; case 0xe0:return 133; case 0xe7:return 135;
      case 0xf1:return 164; case 0xed:return 161; case 0xf3:return 162;
      case 0xfa:return 163; case 0xe1:return 160; case 0xee:return 140;
      case 0xef:return 139; case 0xe2:return 131; case 0xc9:return 144;
      case 0xfb:return 150;
      case 0xa1:return 173; case 0xbf:return 168;
    }
  }
  return '?';
}
inline unsigned length(const char* p) { unsigned n=0; while(*p) { glyph(p); ++n; } return n; }
}
