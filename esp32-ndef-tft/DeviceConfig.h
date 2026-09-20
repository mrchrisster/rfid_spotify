#pragma once
// Build with -DPLAYER_HAS_DISPLAY=0 for a screenless RFID player.
// Authentication and control remain available through the same web UI.
#ifndef PLAYER_HAS_DISPLAY
#define PLAYER_HAS_DISPLAY 1
#endif
#if PLAYER_HAS_DISPLAY != 0 && PLAYER_HAS_DISPLAY != 1
#error "PLAYER_HAS_DISPLAY must be 0 or 1"
#endif

// This installation uses an internal LAN and does not require web UI login.
// Set to 1 to restore password prompts on HTTP administration and HTTPS reconnect.
#ifndef PLAYER_REQUIRE_WEB_AUTH
#define PLAYER_REQUIRE_WEB_AUTH 0
#endif
#if PLAYER_REQUIRE_WEB_AUTH != 0 && PLAYER_REQUIRE_WEB_AUTH != 1
#error "PLAYER_REQUIRE_WEB_AUTH must be 0 or 1"
#endif
