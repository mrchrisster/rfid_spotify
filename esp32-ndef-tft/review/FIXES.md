Fix implementation — 2026-09-19

The original REVIEW.md and failing legacy reproduction are preserved as historical evidence. Production sources now use a replacement read path; the legacy reproduction is intentionally not part of the passing regression suite.

| Original finding | Implemented change |
| --- | --- |
| 1. Ultralight stack overflow | Removed NDEF_MFRC522 dependency; read into a fixed 18-byte scratch buffer and copy bounded data, including partial final page groups. |
| 2. Empty artist vector | Candidate catalog state commits only after successful validation. Every selection checks bounds; failed playback retains its position. |
| 3. Reboot overwrites token | Existing NVS token always wins; firmware token seeds only missing storage. |
| 4. Split speaker state | SpotifyClient owns name and ID; only the network task accesses it. All commands use that state. |
| 5. Stale device recovery | Device 404 and Wi-Fi transitions clear the authoritative ID; subsequent commands rediscover. |
| 6. Unsafe NDEF bounds | Fixed-size raw buffers, full message bounds/flag/record validation, checked Text/URI fields; no tag-controlled allocations. |
| 7. Polling blocked by network | Independent hardware, network and dashboard tasks; bounded queues, scheduled playback retries and explicit transport deadlines. |
| 8. Incomplete reader health | Known-version, antenna and timer-configuration checks, read-error recovery and explicit polling/error metrics. |
| 9. Credential exposure | No token-response logging; Digest-protected dashboard; unauthenticated Telnet removed; real TLS trust bundle; secrets excluded from source control. |
| 10. Discarded chunked JSON | Read through HTTPClient's decoded stream into a bounded sink, independent of Content-Length. |
| 11. Partial artwork accepted | Size/time bounds, complete decoded transfer, JPEG validation before replacing cache, optional low-memory skip. |
| 12. Destructive token update | Candidate exchange before commit; nonempty token/expiry validation; checked persistence with retry for flash failure. |
| 13. Missing rate-limit handling | Shared Retry-After cooldown, no immediate replay; bounded scheduled transient playback retries. |
| 14. URI/track mismatch | Supports Text/URI records, strict Spotify type/ID normalization, track arrays and top-level position_ms. |
| 15. False control success | Read live playback/volume state, honor restrictions, publish actual response codes; validate and transfer speaker selection before changing target. |
| 16. Python update timeout | Asynchronous token-install jobs and status polling independent of device discovery; one-use OAuth state, callback startup ordering, request timeouts and safe token storage. |

Additional changes: timestamped bounded logs, reset reason, heap/stack/poll diagnostics, mDNS service rebinding, early SPI deselection, removal of fake CPU-utilization display, pinned build/dependency versions, tests and setup documentation. Local credential values were preserved in secrets.h; no credentials were rotated or sent to Spotify during development.

Verification: generic ESP32 build with core 3.3.11 succeeds. Native ASan/UBSan parser tests include 50,000 malformed cases; RFID tests cover all supported Type-2 capacities and Classic sector mapping; actual SpotifyClient source is tested against mocked transport using real ArduinoJson; seven Python workflow tests pass. Dashboard script syntax is checked separately. Hardware, live OAuth/Spotify/Echo interactions and multi-day soak testing have not been performed. No device was flashed.

The last generic build occupies approximately 1.28 MB (97% of the default 1.25-MiB application partition), with 56,928 bytes static RAM (17%). Free heap at runtime is lower than the static estimate because tasks, queues, TLS, JSON and artwork allocate dynamically. The build still reports existing JPEGDecoder 2.0.0/picojpeg warnings; that library is pinned and unmodified. Future display/feature growth may require a larger application partition after confirming the real board and flash layout.

Administration changes are deliberate: login credentials come from USB Serial at boot, commands are asynchronous, legacy form/Telnet clients need updating, and outbound HTTPS now requires working NTP. LAN HTTP Digest provides access control, not transport encryption. See README.md for provisioning and hardware checks.

Follow-up flash experiment: added a production `PLAYER_HAS_DISPLAY=0` option and a dashboard `has_display` capability flag. The screenless build removes TFT/JPEG/artwork while preserving web administration and token updates. HTTPS/PKCE remains isolated under experiments/https_flash; it is not enabled in production. See that directory's measurement.json and README.md for the five measured variants.

## Production HTTPS reconnect follow-up

The footprint-only probe has been followed by a complete device-hosted HTTPS/PKCE reconnect implementation for both TFT and screenless ESP32-C6 builds. Tokens and grant type are persisted together; legacy tokens migrate on boot. See [REAUTH.md](REAUTH.md) for final build sizes and tests, and [DEVICE_REAUTH.md](../DEVICE_REAUTH.md) for certificate trust, registration, renewal and hardware verification. No device has been flashed or live OAuth login tested.

## Automatic certificate lifecycle

Annual certificate rebuilding is superseded by device-side renewal using a constrained delegated signing key. Real Mbed TLS generation and independent OpenSSL verification pass; both C6 variants still fit. See [CERTIFICATE_RENEWAL.md](CERTIFICATE_RENEWAL.md) for implementation, measurements and remaining hardware checks. The newly generated CA must be trusted once before deployment.

## Internal-LAN access and reset diagnostics

Disabled device login prompts by default for both the dashboard and HTTPS reconnect at the user's request. `PLAYER_REQUIRE_WEB_AUTH=1` preserves optional authentication. Mutation header, Origin, CSRF, state and browser-cookie validation remain. Both HTTPS authentication modes pass native handler tests. Reset logs/dashboard now name the reset cause, and certificate activation explicitly logs an HTTPS-only restart. The supplied transcript contains one new-firmware boot (`ESP_RST_POWERON`) and the expected first-certificate HTTPS reload; it does not establish repeated device crashes. Its three older-format discovery entries are three different speaker/group targets, followed by selection of Van Baby.

## Spotify HTTP 411 playback fix

The pinned ESP32 HTTPClient adds Content-Length only for nonempty payloads. Playback controls used empty PUT/POST bodies without an explicit length, which Spotify rejected with HTTP 411. `SpotifyClient::CallAPI` now sends `Content-Length: 0` on those requests, including pause/resume, next, volume and shuffle. JSON playback bodies retain their automatically calculated nonzero lengths; GET requests are unchanged. Regression tests cover those endpoints and make the fake transport reject missing lengths on empty mutations. Card playback also uses the corrected shuffle request before starting playback.

## Held-card repeat/recovery and reconnect-form corrections

- The existing debounce released a UID after only three RF timeouts (~300 ms). Replaced it with 1.5 seconds of continuous healthy-reader absence; successful same-UID sightings suppress replay, and selection errors/reader recovery preserve the latch. Different UIDs remain immediately eligible. Host regressions reproduce the held-card timeout pattern, fault/recovery periods and millis rollover.
- MFRC522 1.4.12 `PCD_Init()` can leave its configured reset pin as INPUT. The old recovery path then used digitalWrite LOW/HIGH without restoring OUTPUT. Application code now owns the reset pin explicitly and passes `UNUSED_PIN` to the library; tests verify actual output-mode pulses and no library reset-pin ownership. Recovery validates the full register set both before and after reset and logs version, antenna, timer, prescaler, command and reset-pin level. This fixes a concrete recovery bug but does not prove the cause of the observed peripheral fault or spontaneous ESP32 reset.
- Reconnect used `Referrer-Policy: no-referrer` together with mandatory Origin matching. Browsers can send `Origin: null` on native form POSTs under that policy (see https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/Referrer-Policy). Changed to strict-origin, preserving origin validation while omitting URL paths/query codes from referrers. Cookie, origin, session and form-size failures now have distinct messages. Host tests assert the policy and reject missing-cookie/null-origin requests. Browser trust still requires installing the correct local CA; proceeding through its warning is not equivalent.
- The user confirmed a later ROM restart occurred spontaneously, with the display black and RFID already failing at startup. No panic/watchdog diagnostic is present in the supplied excerpt; the second boot is truncated. Power/reset-line behavior remains an unconfirmed possibility. No hardware upload, power-cycle or serial-port manipulation was performed by the agent. Firmware now logs its build stamp, TFT build mode and completed display initialization/draw calls; this does not claim panel communication is verified.

## RFID recovery diagnostics and reset spacing — 2026-09-20

Recovery logs now identify startup, dashboard requests, periodic register checks,
register faults after RF timeouts, and consecutive card-operation failures.
Wakeup/selection failures retain the MFRC522 status code and description; NDEF
I/O failures also report the operation, block/page address, and returned length.
Selection calls the same `PICC_Select` operation previously wrapped by
`PICC_ReadCardSerial`, preserving behavior while exposing its status.

All reset triggers share a five-second minimum interval, including failed resets.
A deferred recovery pauses card polling until a scheduled health check; the held
card identity remains latched. This prevents the back-to-back reset pattern in
field logs without claiming to fix the underlying intermittent register faults.
Regression coverage includes the cooldown boundary, millis rollover, and reader
error context. Compare TFT and screenless builds on the same hardware to help
isolate display activity; physical SPI/power faults still require device testing.

## Companion screen and concise reader logs

The TFT info screen now draws a static headphone-wearing companion, a contextual
card/connection prompt, and a Wi-Fi/Spotify/reader status strip with the local
web address. The network worker publishes authorization flags atomically; the
hardware worker remains the sole display owner. Only changed status regions
redraw, checked once per second while the info screen is visible. Artwork,
manual clear, screenless operation, and the existing timeout retain their roles.

Reader logs default to one recovery summary, with continuous failure warnings
limited to once per 30 seconds. `PLAYER_RFID_DEBUG=1` restores individual error
and register diagnostics. Recovery intervals and diagnostic counters are not
changed by this logging preference. Successful playback does not establish that
the intermittent register fault has been resolved.

## Startup DNS/SNTP assertion — 2026-09-20

The user-reported ELF SHA prefix `fd184a391` exactly matched the retained TFT
build (`fd184a3911e8fd4bb8ffbc7b62fc70afa19a1ae09b2c8b25e69993c177ee049a`).
Decoding its backtrace identified this call chain:

```
networkWorker -> EnsureTokenFresh -> exchange -> HTTPClient::POST
 -> NetworkClientSecure::connect -> NetworkManager::hostByName
 -> dns_clear_cache -> dns_call_found -> sntp_dns_found
 -> sntp_try_next_server -> dns_gethostbyname -> udp_new_ip_type (assert)
```

The pinned Arduino-ESP32 3.3.11 `NetworkManager.cpp` calls `dns_clear_cache()`
outside the TCP/IP core when interface address availability changes. Clearing
an outstanding SNTP lookup invokes its callback on the caller's task; attempting
the next SNTP server then hits lwIP's thread-safety assertion. This is a decoded
network crash, separate from the intermittent RFID register fault.

`SafeTlsClient.h` bypasses that Arduino hostname resolver for the player's
outbound HTTPS, using lwIP's socket `getaddrinfo` API and the secure client's
IP-plus-hostname overload. The original hostname is retained for SNI and
certificate verification; the existing CA bundle, credentials, and connection
timeout remain in effect. This adapter targets the player's IPv4 Wi-Fi setup;
it does not add IPv6-only or PSK support. No installed core files are modified,
no network safety checks are disabled, and no TCP/IP lock is held during TLS.

The regression test checks virtual dispatch, resolver failure, malformed results,
address cleanup, hostname/credential forwarding, and TLS failure propagation.
The default TFT setting is restored to 1 so deployment of this fix matches the
reported hardware; the screenless option remains available for reader diagnosis.

## Screen language and larger logo layout

The dashboard Display section now saves a screen language (en/de/fr/es) to a
separate `player_ui` NVS namespace owned by the HTTP loop. Validated writes happen
before the atomic language selection changes; failed writes retain the previous
selection. German is the default for devices without a valid saved choice.
The hardware task redraws the visible info screen after a language change; album
artwork is not interrupted. The maintenance UI and logs stay in English.

The GFX classic font receives explicit UTF-8-to-CP437 conversion for supported
accents. Host checks cover language IDs, default fallback, every translated
message's display width, accent mapping, and dashboard save/failure behavior.
The headphone character is enlarged at right; the original English logo wording
is rendered as separate large text at left, above the localized prompt and
connectivity strip.

## Offline Wi-Fi setup

`WifiSetup.cpp` owns Wi-Fi connection attempts and a separate `wifi_setup` NVS
namespace on the HTTP loop. The old concurrent automatic/reconnect loop is
removed. Compiled credentials are bootstrap/fallback values; one atomic JSON
record stores a validated replacement SSID/password. Wi-Fi driver persistence
is disabled, so test credentials cannot replace the durable record on failure.

A WPA-protected SoftAP opens after 30 seconds disconnected or a dashboard request.
Its random per-device password is saved separately and shown on the TFT, USB
Serial, and authorized dashboard (never shared diagnostic logs). Portal routes
check that the receiving socket is on the AP interface; mutations also require
the existing custom browser request header. Network names render as text, not
HTML. Captive DNS uses the core's DNSServer/AsyncUDP implementation; its UDP setup
is guarded by the core lock in the pinned version. The explicit HTTP IP is a
fallback when phone captive-portal detection does not launch.

The trial waits for an IP on the requested SSID and a stable three-second
connection, with a 30-second deadline. Timeout/write failure restores the old
network and keeps the portal open. Success closes the portal after 30 seconds;
cancellation is deferred until after the HTTP response. Async scanning avoids
blocking the hardware worker. TFT instructions override cover art/blanking while
setup is active, with atomic state snapshots and immutable setup-name/password
buffers; setup is also usable without a TFT.

Host tests execute the production manager with fake Wi-Fi, NVS, and HTTP. They
cover timing rollover, fallback entry, AP-only routing/header checks, malformed
credentials, duplicate trials, timeout/write-failure rollback, unstable connection,
commit, restoration on boot, and cancellation. Browser tests cover translations,
credential submission, retry controls, text-only SSIDs, and retaining success
instructions after the setup network closes. Actual AP/channel switching and
phone captive-portal behavior still need on-device verification.

## Default speaker in setup

Wi-Fi success now links to the default-speaker section of the player's dashboard
on its new LAN address, after asking the phone to rejoin home Wi-Fi. This remains
a two-step flow: the offline portal cannot discover Spotify speakers until Wi-Fi
and Spotify authorization work. The TFT success screen shows the new web address
and a localized prompt to choose the default Echo. The dashboard labels and
preselects the current default and reports completion/failure of the actual
selection job. A successful selection is persisted by name for rediscovery after
reboot. Setup transfer uses `play:false`; failed storage no longer switches the
firmware's in-memory default (although the transfer may already have occurred).

## Display settings and automatic portal closure

`/api/display_settings` validates and atomically persists live-log mode plus a
cover timeout as a single NVS integer. The settings apply only after successful
storage. Cover time allows 0 (indefinite) or 10–86400 seconds, with a default of
600 seconds; unsigned elapsed-time arithmetic handles millis rollover. Audio and
the info-screen timeout are independent. Headless builds reject display changes.

The HTTP loop assembles a bounded 18x52-character application-log snapshot and
sends it through a length-one overwrite queue. Only the hardware worker reads
and draws it; no Arduino String history is shared across tasks. Long messages
wrap, snapshots coalesce, and drawing happens at most once per second while new
messages exist. Reader polling runs between log rows. Debug mode overrides
artwork/manual screen commands but retains the latest validated artwork in RAM.
Wi-Fi setup has higher priority; disabling debug restores the cached cover or
info screen. Private USB-only setup credentials are not added to this log.

Automatic fallback portals now close after ten seconds continuously connected
to the saved SSID with an IP. Manual portal starts or explicit scan/connect
activity disable automatic closure, preserving the interactive setup session.
The production-manager tests cover recovery, an interrupted stability interval,
and preservation of a manually opened portal. Additional host tests cover log
buffer bounds/wrapping, timeout rollover, setting limits, and dashboard
validation/save failures without overwriting unsaved edits.


## Cover loading diagnostics and dashboard refresh

Cover failures previously returned silently, leaving a timed-out screen black,
and Last cover did nothing without a cached image. Artwork now uses the resolved
album or track context where possible, chooses a suitable image independent of
array order, and falls back to matching playback metadata when the bounded album
response fails. Idle-worker retries are limited to three attempts and do not
restart playback. Download allocation can shrink below the 64 KB maximum while
retaining a memory reserve. Metadata, memory, download, queue and decode failures
are logged. Last cover fetches if empty; Reload cover explicitly downloads again.

Page initialization no longer posts a speaker-discovery command. Host tests run
the full dashboard script to check that loading/polling sends no POST, and cover
metadata selection, stale playback detection and transport error reporting.
Firmware compilation does not replace testing actual Spotify artwork and TFT
rendering on the device.


## Startup screen after covers

The existing info-screen control is now labeled **Show startup screen**. Cover
timeout returns to the logo/status screen for 30 minutes before blanking. The
same 30-minute duration applies at boot and when opened manually. Cover time 0
still means indefinitely visible, and Clear still blanks immediately. Wi-Fi
setup and debug modes retain priority. Timeout activation uses visible screen
state rather than a zero timestamp sentinel, preserving operation at millis wrap.


## Responsive dashboard layout

Reorganized the dashboard into Player, Settings and Diagnostics with hash-based
navigation and working provisioning links to speaker settings. Embedded the
existing logo PNG, added text-based connection badges and failure alerts, and
kept credentials/advanced settings in collapsed sections. Playback controls stay
on the default page; hardware recovery controls stay in Diagnostics. Logs fetch
only in Diagnostics and can be paused/copied, including on local HTTP. Existing
settings/job handlers are retained; display controls hide on headless devices.
The status endpoint adds Wi-Fi connection state and SSID for accurate badges.
Native dashboard tests cover navigation, no on-load commands, visibility, status
recovery, log polling/pause/copy and existing settings/token workflows.


## Oversized artwork fallback

A local 413 means the JPEG exceeds the allocated buffer, not a playback failure.
The adaptive buffer previously subtracted its reserve from the largest free
block, reducing usable contiguous space unnecessarily. Capacity now leaves a
48 KB reserve in total free heap, allows allocator slack in the contiguous
block, and remains capped at 64 KB. It logs actual capacity and known JPEG size.
Oversized downloads (including unknown-length overflow) select a smaller
Spotify-provided image in the same attempt. The renderer centers artwork of
different dimensions; cached images remain intact on download failure. Native
tests cover fragmented/low heap budgets, descending image selection, and known
and unknown-length size rejection. Real-device artwork still needs validation.


## Allocate artwork after HTTPS connects

Further device logs showed GET returning -1 within milliseconds after a roughly
49 KB JPEG allocation, leaving about 48 KB free. This does not establish the
exact connection failure but exposes avoidable pressure on the TLS handshake.
The production artwork path now connects and receives headers before allocating
image memory, uses exact Content-Length when known, and caps unknown-length
bodies. The old caller-buffer API remains for compatibility. Failed dynamic
downloads release their allocation. Errors report HTTP, DNS and TLS details.
Native tests assert the buffer is null at GET time, and cover successful,
oversized, truncated, unknown-length and connection-failure responses. The
earlier pre-handshake budget helper has been removed.


## Full-screen artwork and startup readiness

Device evidence: the 300px JPEG was 49,457 bytes versus a 32,148-byte live-TLS
budget, causing a 64px fallback. Known-length covers within the 64 KB limit now
spool through a temporary SPIFFS file when needed, release TLS, then load into
RAM. Only an erased partition may be formatted automatically. Existing data
is preserved on mount failure. Our temporary file is removed on success/failure;
no full-screen framebuffer is allocated. Covers are scaled per MCU into a bounded
320-pixel row, preserving aspect ratio and cropping centrally. Flash fallback
incurs writes only when the preferred download cannot fit in RAM during TLS.

Authentication no longer starts a 30-second maintenance wait while offline or
waiting for time. Invalid access-token state checks every second after readiness,
respecting cooldowns. Early playback stays pending without consuming retries,
while newer play/select requests supersede it. Permanent auth failures surface
instead of waiting forever. Tests cover scaling coordinates, readiness/cooldown
timing, temporary-file cleanup, truncation/write/read failures and safe mounting.
The incomplete reboot log still cannot establish the cause of a reset.

Large JPEGs (>24 KB) are released after drawing instead of being held as a RAM
cache, which would recreate TLS starvation on the next playback or refresh.
The TFT retains its pixels. Last cover re-fetches when no RAM cache remains;
this trades offline large-cover recall for networking headroom. In debug mode,
large artwork is likewise released and disabling debug returns to the info screen.


## Direct flash decoding; reject thumbnail artwork

The older sketch reserves a static 64 KB JPEG buffer (rather than finding a
large free heap block later), selects image index 1, draws native pixels at
(0,-30), and retains that buffer for Last cover. Its setup attempts token refresh
before entering the polling loop. It lacks the new dedicated task stacks, queues
and local HTTPS reconnect service. Its separate SpotifyClient implementation
was not in the supplied sketch, so transport/certificate differences are unknown.

Removed selection of artwork narrower than 240px. The temporary-file approach
previously still allocated a complete JPEG after closing TLS, so it could fail
on a fragmented heap; a combined failure message obscured the exact stage.
Flash downloads now hand exclusive file ownership through the result/display
queues and decode directly with JPEGDecoder's fs::File overload. An atomic busy
flag prevents overwriting queued/in-use artwork. Every discard/failure/consume
path releases the file; no thumbnail is displayed if high-quality loading fails.
Filesystem setup occurs before HTTPS GET, avoiding format delays during body
transfer. Stage-specific logs distinguish mount, open, transfer and decode errors.
Host tests cover file handoff ownership and cleanup plus thumbnail rejection.


## Immediate card loading and error screens

The hardware worker starts a localized storybook loader immediately after a new
UID passes held-card suppression, before NDEF parsing. Animation uses bounded
primitive drawing at 120 ms intervals and no network assets or framebuffer.
Malformed/read-error cards, full queues, final playback errors and exhausted
artwork attempts produce distinct five-second error screens. Loading has a
90-second visual timeout without cancelling queued playback. Wi-Fi setup and
TFT log mode retain precedence; manual display controls dismiss the loader.

Display messages carry job IDs; a reserved ID also orders invalid presentations.
Stale images and errors cannot overwrite a newer presentation. JPEG decoding
aborts if RFID polling detects a new presentation mid-decode. Successful artwork
stops animation before rendering. Host tests exercise loading/error timing,
rollover, stale results, resets and frame throttling. Hardware timings remain
to be measured on the device.
