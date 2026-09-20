#pragma once
#include <Arduino.h>
namespace DeviceIdentity {
void begin();
// Called by the Arduino task. Returns true when HTTPS must reload its certificate.
bool maintain();
// Only after stopping the HTTPS server, whose config borrows chain() bytes.
void activate();
const char* hostname();
const char* key();
const char* chain();
bool automatic();
int certificateDays();
int issuerDays();
const char* renewalStatus();
}
