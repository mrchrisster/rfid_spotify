#pragma once
#include <Arduino.h>
#include <WebServer.h>
namespace WifiSetup {
enum State : uint8_t { Closed, Portal, Testing, Saved, Failed, StorageFailed };
void begin(const char* fallbackSsid,const char* fallbackPassword);
void routes(WebServer& web);
void tick();
void requestStart();
bool active();
State state();
const char* networkName();
const char* setupPassword();
// Intercept AP requests before the normal dashboard or its not-found response.
bool servePortal(WebServer& web);
}
