# Architecture

Updated: 2026-09-20. Paths below are repository-relative unless absolute.

## Product / hardware
- Audiobook player: RFID card → Spotify URI → Spotify Connect playback on chosen Echo. ESP32 controls playback; Echo streams audio. No local audio decoding.
- Physical chip verified by esptool: **original ESP32**, not ESP32-C6. Keep existing wiring. `sketch.yaml` still contains an experimental C6 profile; it is not the deployed target.
- Default board: ESP32 Dev Module, ≥4 MB flash, `PartitionScheme=min_spiffs`: 1,966,080-byte app slots, 128 KB SPIFFS. Layout reserves OTA slots; firmware OTA is **not implemented**.
- Shared SPI: SCK18/MISO19/MOSI23; MFRC522 CS5/reset4; ILI9341 TFT CS15/DC2/reset22; landscape 320×240. Only hardware worker may draw/poll this bus.
- `DeviceConfig.h`: `PLAYER_HAS_DISPLAY=1`, `PLAYER_REQUIRE_WEB_AUTH=0` (explicit internal-LAN user preference), `PLAYER_RFID_DEBUG=0`. Headless build uses `-DPLAYER_HAS_DISPLAY=0`.

## Stack / build
- `sketch.yaml` pins Arduino-ESP32 3.3.11, ArduinoJson 7.4.3, MFRC522 1.4.12, Adafruit ILI9341 1.6.3, GFX 1.12.6, BusIO 1.17.4, JPEGDecoder 2.0.0.
- Root sketch: `esp32SpotifyAlexa_v5_tft_ndef.ino`; Arduino compiles sibling `.cpp` files.
- Installed CLI on current host: `/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli`.
- Normal build: `arduino-cli compile --profile esp32-review .` (may download dependencies).
- Installed-dependency build: `arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=min_spiffs --build-path /private/tmp/player-wifi-tft .`.
- Headless: same FQBN, separate build path `/private/tmp/player-wifi-headless`, plus `--build-property 'compiler.cpp.extra_flags=-DPLAYER_HAS_DISPLAY=0'`.
- Tests: `python3 tests/run_tests.py` → native C++ ASan/UBSan, Python, Node dashboard/portal tests. Uses real ArduinoJson headers at `~/Documents/Arduino/libraries/ArduinoJson/src`; override with `--arduino-json`.
- `tests/run_identity_tests.py` is a separate identity test runner; do not imply the main runner covers everything.
- No `.git` directory present when continuity documentation was initialized; do not assume commits exist.

## Components / ownership
| File(s) | Responsibility / invariant |
|---|---|
| `esp32SpotifyAlexa_v5_tft_ndef.ino` | Coordination, HTTP routes, bounded job/result queues, task-owned state, display/reader loop |
| `SpotifyClient.cpp`, `SpotifyClient.h` | Serialized Spotify API/token/CDN I/O; TLS verification, cooldowns, bounded bodies, token persistence callback |
| `SafeTlsClient.h` | Safe lwIP socket DNS resolver, original hostname retained for SNI/certificate checks |
| `SafeNdef.h`, `RfidReader.h` | Bounded NDEF parsing, tag capacity/sector handling, URI validation |
| `RfidPresence.h`, `RfidRecovery.h` | UID latch, 1.5s healthy absence before rearming; rate-limited hardware recovery |
| `DeviceAuth.cpp`, `OAuthSession.h` | Local HTTPS browser OAuth/PKCE flow, session/state validation, network-worker token exchange |
| `DeviceIdentity.cpp`, `CertificatePolicy.h` | Provisioned delegated identity, automatic server-leaf renewal, NVS persistence |
| `WifiSetup.cpp`, `WifiSetupPolicy.h` | AP provisioning, captive DNS, background reconnect, candidate validation/rollback |
| `PlayerScreen.h`, `PlayerLanguage.h`, `PlayerLogo.h` | Startup/status/provisioning screens; en/de/fr/es, German default, CP437 accent conversion |
| `LoadingScreen.h`, `LoadingState.h` | Immediate card feedback, small animated storybook, localized errors, stale-result/timing policy |
| `Artwork.h`, `ArtworkSpool.h`, `CoverLayout.h` | Quality selection, memory-bounded download/file ownership, full-screen aspect-preserving crop |
| `DisplaySettings.h` | Packed persisted debug/timeout settings, bounded TFT log snapshot, rollover-safe timing |
| `StartupPolicy.h` | Auth readiness/maintenance scheduling and early-play waiting criteria |
| `Dashboard.h` | Embedded HTML/CSS/JS/logo; Player, Settings, Diagnostics; no external web dependencies |

### Concurrency
- Arduino/main loop: WebServer, Wi-Fi setup manager/DNS, mDNS, HTTPS lifecycle, UI NVS, drain results/logs, dashboard snapshots. HTTP handlers enqueue work; do not perform Spotify I/O.
- `networkWorker`: sole Spotify client owner; playback resolution, auth/token writes, artwork download. Stack 14,336 bytes, priority1.
- `hardwareWorker`: sole RFID/TFT/JPEGDecoder owner; stack8,192 bytes, priority2. RFID polling target100ms, also between JPEG blocks/log rows.
- Queues: jobs4, results8, displayJobs3, log lines24; TFT log snapshot length1 overwrite. Queue elements are POD; heap buffer ownership transfers explicitly.
- `ArtworkSpool::busy`: exclusive scratch-file ownership transfers network → result queue → display queue → hardware decoder. No overwrite while queued/decoding. Every discard/consume/error path must release ownership/file.
- `LoadingState` is hardware-owned. Display messages carry job IDs; reserve an ID even for invalid cards to reject stale earlier results. JPEG decoding aborts if polling sees a new presentation mid-decode.

## Persistence / credentials
| Namespace / location | Contents / owner |
|---|---|
| NVS `spotify` | `auth_v2` JSON `{token,pkce}`, `device_name`, optional `admin_pass`; setup + network-owned updates |
| NVS `player_ui` | `language`, packed `display` (high bit debug, remaining bits seconds); main loop |
| NVS `wifi_setup` | Credentials record + generated `setup_key`; Wi-Fi manager |
| NVS `tls_identity` | Renewed leaf identity; identity/main lifecycle |
| SPIFFS `/.rfid-cover-download.jpg` | Transient artwork only; removed after consume/failure; leftovers removed on next download |
| Ignored local files | `secrets.h`, `DeviceCertificate.h`, `https-private/`, `https-auto-private/`, `https-auto-private-explicit-backup/`; **never copy secrets into docs** |
- Saved settings take precedence over boot defaults. Do not erase flash or change partitions casually.
- `tools/provision_https.py`, `DEVICE_REAUTH.md`: provisioning/callback details. Exact configured hostname callback must match Spotify registration; web UI access by IP does not rewrite the OAuth callback.
- Automatic leaf renewal does not imply perpetual issuer validity or perpetual Spotify authorization. No guaranteed six-month refresh-token lifetime is established by this project.

## Runtime behavior
- Card latch suppresses held-card retriggers; uncertain reader health does not imply removal. Recoveries require ≥5s spacing; verbose dumps opt-in, ordinary failures rate-limited.
- Artist cards resolve an album before play; playlist ordering advances after success. Saved speaker name is rediscovered into a current device ID.
- Startup waits for Wi-Fi + valid clock before TLS. Invalid access-token state checks every1s, normal maintenance30s, obeying cooldowns. Early play remains pending without spending playback retries; newer play/select supersedes it. Permanent auth errors surface.
- Wi-Fi fallback AP starts after30s offline; saved Wi-Fi retries continue15s apart. Candidate connection:30s deadline, 3s stable association+DHCP before commit; not an internet test.
- Automatic fallback AP closes after10s stable saved-network recovery. Manual setup/scan/connect activity keeps it open. Successful new-network setup closes after30s and links to `?setup=speaker#speakerSetup`.
- Screen priority: Wi-Fi setup > TFT debug > loading/cover/startup. Log mode draws ≤1Hz. Loader frame interval120ms, error5s, loading visual timeout90s (does not cancel playback).
- Cover duration UI: **minutes**, decimals allowed, 0=indefinite, default10min, maximum1440min. HTTP/NVS remain **seconds**, valid 0 or10..86400; browser converts/rounds, preserving legacy saved seconds.
- Cover expiry → startup screen30min → blank; audio unaffected. Startup button/boot also use30min.
- Dashboard polling is read-only. Speaker discovery requires Find speakers. Logs fetched only in Diagnostics when not paused; clipboard fallback supports HTTP. TFT controls hidden on headless devices.

## ADRs / intent
| ID | Decision / reason / trade-off |
|---|---|
| A01 | Task ownership and bounded queues keep network waits out of RFID/TFT service. Never add drawing from HTTP/network tasks. |
| A02 | Retain certificate-verified TLS. Original client used `setInsecure()`; restoring it would hide certificate/time problems rather than solve memory issues. |
| A03 | `SafeTlsClient` bypasses Arduino core3.3.11's unsafe DNS cache-clear path. A decoded crash showed SNTP/raw UDP invoked without TCPIP lock. Use `lwip_getaddrinfo`, preserve hostname, never hold TCPIP lock across blocking DNS/TLS. |
| A04 | Device-hosted PKCE reconnect supports screenless players and avoids a required Python/external callback server. Local HTTPS certificate trust remains a browser setup constraint. |
| A05 | **Never use thumbnails <240px**. Prefer the smallest supported width≥240 (normally300px), maximum640. User explicitly rejected pixelated64px fallback. |
| A06 | Establish HTTPS before allocating image RAM. Known-length image gets exact allocation where possible; 64KB limit, working-memory reserve. Metadata JSON bounded24KB; large album responses fall back to matching current playback metadata. |
| A07 | If preferred JPEG cannot fit during TLS, spool to SPIFFS and decode the **file directly**. User logs showed46–58KB JPEGs vs23–32KB budget. Copying back into RAM after TLS also failed; do not reintroduce it. File path costs flash writes but preserves quality/heap. |
| A08 | Mount/initialize filesystem before GET. Autoformat only a wholly erased partition; preserve existing nonempty data on mount failure. JPEGDecoder aliases `SPIFFS` to `LittleFS`; sketch undefines that macro and passes an explicit `fs::File` to `decodeFsFile`. |
| A09 | RAM JPEGs >24KB released after drawing to preserve next TLS request's headroom. TFT retains pixels. Last cover may re-download and requires network; no promise of offline large-cover recall. |
| A10 | Original sketch reserved a static64KB JPEG buffer and decoded `images[1]` at native size `(0,-30)`. Additional current stacks/queues/HTTPS service changed RAM pressure. Static memory is not free memory; do not restore that buffer blindly. |
| A11 | Loader draws immediately after new UID detection, before NDEF/API work; only its small animation region changes. Preserve RFID polling; do not add animation delays/full-frame buffers. |

## Further references
- `README.md`: user setup/build/UI; `DEVICE_REAUTH.md`: certificate/OAuth setup; `review/FIXES.md`: detailed implementation history.
- `assets/README.md`, `tools/encode_player_logo.py`: logo provenance/regeneration. Existing bitmap is stored in flash; dashboard embeds a small PNG.
- `.ai/SCRATCHPAD.md`: current verification gaps and next steps; `.ai/CHANGELOG_AI.md`: append-only notable change record.
