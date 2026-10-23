#pragma once
#include "PlayerScreen.h"
namespace LoadingScreen {
// Error kinds: unsupported card, read failure, playback, cover, busy.
inline const char* title(uint8_t language,uint8_t error) {
 static const char* words[4][6]={
  {"A story is coming!","Unknown story card","Couldn't read card","Playback needs help","Cover unavailable","One moment..."},
  {"Deine Geschichte...","Unbekannte Karte","Lesefehler","Wiedergabe fehlt","Cover fehlt","Einen Moment..."},
  {"Place à l'histoire !","Carte inconnue","Lecture impossible","Lecture en attente","Image indisponible","Un instant..."},
  {"¡Llega una historia!","Tarjeta desconocida","Error de lectura","Error al reproducir","Portada no disponible","Un momento..."}
 };
 return words[language<PlayerLanguage::Count?language:PlayerLanguage::Default][error<=5?error:3];
}
inline const char* hint(uint8_t language,uint8_t error) {
 static const char* words[4][6]={
  {"A little adventure is opening","This card has no valid Spotify story","Remove the card and try again","Check Spotify and your speaker","Audio may continue. Try Reload cover","The player is busy. Try again shortly"},
  {"Ein kleines Abenteuer beginnt","Keine gültige Spotify-Geschichte auf der Karte","Karte entfernen und erneut auflegen","Prüfe Spotify und deinen Lautsprecher","Ton kann weiterlaufen. Cover im Web neu laden","Player beschäftigt. Gleich erneut versuchen"},
  {"Une aventure se prépare","Pas d'histoire Spotify valide sur cette carte","Retire la carte et réessaie","Vérifie Spotify et ton enceinte","Le son peut continuer. Recharge l'image","Lecteur occupé. Réessaie dans un instant"},
  {"Una aventura está por empezar","Esta tarjeta no tiene una historia Spotify válida","Retira la tarjeta y vuelve a intentarlo","Revisa Spotify y tu altavoz","El audio puede seguir. Recarga la portada","Reproductor ocupado. Inténtalo de nuevo"}
 };
 return words[language<PlayerLanguage::Count?language:PlayerLanguage::Default][error<=5?error:3];
}
inline void frame(Adafruit_GFX& d,unsigned phase) {
 using namespace PlayerScreen;
 d.fillRect(96,38,128,102,Navy);
 // An open storybook with a turning page and twinkling story sparks.
 d.fillRoundRect(113,70,46,55,5,Teal);d.fillRoundRect(161,70,46,55,5,Teal);
 d.fillRoundRect(116,67,41,52,4,White);d.fillRoundRect(163,67,41,52,4,White);
 d.drawLine(160,68,160,126,Muted);
 for(int y=80;y<=104;y+=8){d.drawLine(122,y,148,y,Muted);d.drawLine(172,y,197,y,Muted);}
 static const int flip[]={0,9,18,27,36,27,18,9};int x=160+flip[phase%8];
 d.fillTriangle(160,69,x,60,160,118,Yellow);d.drawLine(x,60,160,118,Teal);
 for(int i=0;i<3;++i){int sx=110+i*49,sy=49+(i%2)*7;uint16_t color=(phase+i)%3==0?Yellow:Panel;
 d.drawLine(sx-3,sy,sx+3,sy,color);d.drawLine(sx,sy-3,sx,sy+3,color);}
 for(int i=0;i<3;++i)d.fillCircle(146+i*14,215,3,phase%3==unsigned(i)?Teal:Panel);
}
inline void show(Adafruit_GFX& d,uint8_t language,uint8_t error) {
 using namespace PlayerScreen;
 d.fillScreen(Navy);frame(d,0);
 text(d,155,title(language,error),2,error?Yellow:White);
 text(d,184,hint(language,error),1,Muted);
 if(error){
   d.fillRect(96,38,128,102,Navy);d.drawRoundRect(131,53,58,75,8,Yellow);
   text(d,69,"!",4,Yellow);d.fillRect(135,207,50,16,Navy);
 }
}
}
