#pragma once
#include <Adafruit_GFX.h>
#include "PlayerLogo.h"
#include "PlayerLanguage.h"

// Fixed 320x240 layout. Only the hardware worker may draw on the shared SPI bus.
namespace PlayerScreen {
constexpr uint16_t Navy=0x10E4, Panel=0x1947, Yellow=0xFE88, Teal=0x4E97,
                   Coral=0xFBAC, White=0xEF7D, Muted=0x9D36;
inline void text(Adafruit_GFX& d, int y, const char* value, uint8_t size, uint16_t color) {
  d.cp437(true);
  d.setTextWrap(false); d.setTextSize(size); d.setTextColor(color);
  d.setCursor((320-int(PlayerLanguage::length(value))*6*size)/2,y); while(*value) d.write(PlayerLanguage::glyph(value));
}
inline void illustration(Adafruit_GFX& d) {
  d.fillScreen(Navy);
  d.drawRGBBitmap(320-PlayerLogo::Width-8,4,PlayerLogo::Pixels,
                  PlayerLogo::Width,PlayerLogo::Height);
  d.setTextWrap(false); d.setTextColor(White);
  d.setTextSize(3); d.setCursor(12,39); d.print("SPOTIFY");
  d.setTextSize(2); d.setTextColor(Teal);
  d.setCursor(12,78); d.print("RFID CARD");
  d.setCursor(12,101); d.print("PLAYER");
  d.fillRoundRect(8,191,304,43,8,Panel);
}
inline void badge(Adafruit_GFX& d, int x, const char* label, const char* state, bool ok) {
  d.fillCircle(x,202,3,ok?Teal:Yellow);
  d.setTextSize(1); d.setTextColor(White); d.setCursor(x+7,199);
  d.print(label); d.print(' '); d.print(state);
}
inline void status(Adafruit_GFX& d, bool wifi, bool reader, uint8_t auth, const char* ip, uint8_t language) {
  const auto& copy = PlayerLanguage::get(language);
  const bool token=auth&1, reconnect=auth&2, unsaved=auth&4;
  d.fillRect(0,151,320,36,Navy);
  unsigned state = !wifi ? 0 : reconnect ? 1 : !reader ? 2 : !token ? 3 : unsaved ? 4 : 5;
  text(d,153,copy.title[state],2,state==5?White:Yellow);
  text(d,176,copy.hint[state],1,Muted);
  d.fillRoundRect(8,191,304,43,8,Panel);
  badge(d,17,copy.wifi,wifi?"OK":copy.wait,wifi);
  badge(d,110,"Spotify",reconnect?copy.login:token?"OK":copy.wait,token&&!reconnect);
  badge(d,233,copy.reader,reader?"OK":copy.wait,reader);
  text(d,219,ip,1,Muted);
}
inline void wifiSetup(Adafruit_GFX& d, uint8_t language, const char* network, const char* password, uint8_t state, uint8_t flags, const char* connectedUrl) {
  // Keep network and password unlocalized so phones see exactly the same values.
  static const char* words[4][11]={
    {"Wi-Fi setup","1. Connect your phone to:","Password:","2. Open in your browser:","Choose your home Wi-Fi", "Testing connection...", "Saved! Return to home Wi-Fi", "Failed. Please try again", "Save failed. Please retry", "Stay connected: no internet is normal","3. Choose the default Echo in the web UI"},
    {"WLAN einrichten","1. Handy mit diesem WLAN verbinden:","Passwort:","2. Im Browser öffnen:","Wähle dort dein Heim-WLAN", "Verbindung wird geprüft...", "Gespeichert! Zurück ins Heim-WLAN", "Fehlgeschlagen. Erneut versuchen", "Speichern fehlgeschlagen", "Verbunden bleiben, auch ohne Internet","3. Standard-Echo im Web wählen"},
    {"Configurer le Wi-Fi","1. Connecte ton téléphone à :","Mot de passe :","2. Ouvre dans le navigateur :","Choisis ton Wi-Fi", "Connexion en cours...", "Enregistré ! Rejoins ton Wi-Fi", "Échec. Réessaie", "Échec de sauvegarde", "Reste connecté, même sans Internet","3. Choisis un Echo par défaut sur le web"},
    {"Configurar Wi-Fi","1. Conecta el móvil a:","Contraseña:","2. Abre en el navegador:","Elige tu Wi-Fi", "Probando conexión...", "¡Guardado! Vuelve a tu Wi-Fi", "Error. Inténtalo de nuevo", "Error al guardar", "Sigue conectado aunque no haya Internet","3. Elige el Echo predeterminado en la web"}
  };
  const auto& w=words[language<PlayerLanguage::Count?language:PlayerLanguage::Default];
  d.fillScreen(Navy);text(d,8,w[0],2,White);
  text(d,34,w[1],1,Muted);text(d,49,network,1,Teal);
  text(d,68,w[2],1,Muted);text(d,81,password,2,White);
  text(d,108,w[3],1,Muted);text(d,123,"192.168.4.1",2,Teal);
  unsigned message=state==2?5:state==3?6:state==4?7:state==5?8:4;
  text(d,154,w[message],1,Yellow);text(d,174,w[state==3?10:9],1,Muted);
  d.fillRoundRect(8,191,304,43,8,Panel);
  const auto& c=PlayerLanguage::get(language);
  badge(d,17,c.wifi,(flags&2)?"OK":c.wait,flags&2);
  badge(d,110,"Spotify",(flags&4)?"OK":c.wait,flags&4);
  badge(d,233,c.reader,(flags&1)?"OK":c.wait,flags&1);
  text(d,219,state==3?connectedUrl:"http://192.168.4.1",1,Muted);
}

}
