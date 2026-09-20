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
