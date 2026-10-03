#pragma once
#include <Arduino.h>
class SpotifyClient;
namespace DeviceAuth {
// begin/tick belong to the Arduino loop; process belongs exclusively to the Spotify worker.
void begin(const String& clientId, const String& adminPassword);
void tick(bool connected);
bool process(SpotifyClient& spotify);
bool ready();
int certificateDays();
const char* hostname();
String url();
}
