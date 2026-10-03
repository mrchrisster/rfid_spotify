Review of ESP32 Spotify RFID player — 2026-09-19

The current code has confirmed correctness and memory-safety defects. It should not yet be considered reliable for unattended operation. The two-day reader failure cannot be assigned a single cause without logs and hardware testing.

Reviewed all six project source/configuration files, the locally installed NFC/MFRC522 implementations, and relevant ESP32 HTTP/TLS code. This directory has no Git history, so findings concern the current snapshot; attribution to a particular recent change is not possible. Firmware and installed libraries were not modified.

**Validation performed**

- Built successfully for `esp32:esp32:esp32` with ESP32 core 3.3.11 and the locally installed libraries. This is a generic ESP32 build, not confirmation of the deployed board settings.
- Build size: 1,225,447 bytes flash (93% of the default application partition); 121,936 bytes static RAM (37%). These figures do not establish runtime heap headroom.
- Installed versions: NDEF_MFRC522 2.0.1, MFRC522 1.4.12, ArduinoJson 7.4.3, JPEGDecoder 2.0.0. ArduinoJson deprecations and several format/library warnings were emitted.
- Both Python scripts pass syntax parsing. OAuth, live Spotify calls, device flashing, and hardware tests were not performed.
- AddressSanitizer reproduced a stack-buffer overflow in extracted, unchanged Ultralight read/buffer-sizing functions, with mocked successful 18-byte RFID responses. See [reproduction](ultralight_repro.cpp) and [output](ultralight-asan.txt). This demonstrates the software defect, not the historical hardware incident.

P1 below means fix before relying on unattended use; P2 means a functional or recovery defect that should also be corrected.

**1. P1 — Installed NFC library writes beyond its stack buffer during ordinary Ultralight/NTAG reads.**

Location: `/Users/chrishelms/Documents/Arduino/libraries/NDEF_MFRC522/src/MifareUltralight.cpp:35–64`, reached from `esp32SpotifyAlexa_v5_tft_ndef.ino:828`.

The loop checks whether enough data has been read using the starting index of the current read, after writing another 18 bytes. For a 40-byte NDEF message at offset 2, it allocates 50 bytes, then reads at offsets 0, 16, 32, and 48. The final write reaches byte 65, sixteen bytes beyond the allocation. The real MFRC522 API expects space for 18 bytes (16 data plus CRC), matching the mock used in the reproduction. An NTAG with those pages available can therefore corrupt the stack even with a well-formed message. Smaller physical tags may instead produce a failed read on the unnecessary extra page.

Fix: read each page group into a fixed 18-byte scratch buffer, copy only the remaining payload bytes, and stop before the next read once the required length is covered. Validate lengths against tag capacity. Vendor/pin the corrected dependency; an application-only change after `nfc.read()` is too late. Confirm which tag types and library versions are actually deployed.

**2. P1 — Failed artist switching leaves an empty vector that is subsequently indexed.**

Location: `esp32SpotifyAlexa_v5_tft_ndef.ino:927–943, 975–989`.

Reproduction sequence: successfully scan artist A; scan artist B while its album-count request fails; scan A again. B's attempt clears `albumPlaylistIndices`, then returns without changing `lastArtistId`. A now skips initialization, resets the index to zero, and reads `albumPlaylistIndices[0]` from an empty vector. This is undefined behavior; retained vector capacity does not make the access valid. A zero-album response produces the same state.

Fix: build replacement artist state in temporary variables and commit it only on success. Guard emptiness before indexing, and advance the selection only once the intended playback succeeds.

**3. P1 — A reboot overwrites a newer persisted refresh token with the old compiled token.**

Location: `esp32SpotifyAlexa_v5_tft_ndef.ino:159–172`.

Any nonempty `settings.h` token that differs from NVS wins at every boot. Both automatic rotation and manual token updates save a different token in NVS, so the next restart undoes them. If the compiled token is revoked, a reboot intended to recover RFID also breaks Spotify authentication.

Fix: use NVS as the authority once initialized; seed from settings only when no persisted token exists. If deliberate firmware-driven replacement is needed, use an explicit provisioning version/reset operation.

**4. P1 — Speaker selection does not update the client that actually plays cards.**

Location: `esp32SpotifyAlexa_v5_tft_ndef.ino:89, 175–180, 525–535`; `SpotifyClient.cpp:16–20, 154–164, 316`.

`SpotifyClient` copies the initial speaker name into its own private field before setup. Loading a saved name or choosing a speaker later changes only the sketch's `deviceName` and `currentDeviceId`. Card playback and Next use the client's separate private ID, while pause/volume/shuffle use the sketch ID. Consequently, the dashboard can show speaker B while a card plays on A. Rebooting does not repair the saved-name mismatch.

Fix: give SpotifyClient sole ownership of speaker name/ID and expose a selection method used by setup and the dashboard. All controls should use that state.

**5. P2 — Device recovery retains an obsolete private device ID.**

Location: `esp32SpotifyAlexa_v5_tft_ndef.ino:891–894`; `SpotifyClient.cpp:194–229, 294–298`.

The 404 handler clears only `currentDeviceId`. `ResetState()` does not clear the client's ID, and unsuccessful discovery leaves it intact. Subsequent attempts keep addressing the stale device instead of establishing an explicit unavailable state. Successful discovery can recover it, but failure is misrepresented and Next also continues using it. Spotify explicitly says cached IDs are not guaranteed permanent: [device API documentation](https://developer.spotify.com/documentation/web-api/reference/get-a-users-available-devices).

Fix: invalidate the authoritative ID on device-related failures, propagate discovery failure, and schedule rediscovery without blocking RFID. Device discovery alone is not an Echo wake mechanism; this project contains no Alexa wake integration.

**6. P1 — Malformed NDEF text can corrupt memory, and the installed NDEF decoder has additional unchecked bounds.**

Location: `esp32SpotifyAlexa_v5_tft_ndef.ino:839–845`; installed `NDEF_MFRC522/src/NdefMessage.cpp:19–75` and `NdefMessage.h:7`.

The sketch reads `payload[0]` without checking for an empty payload, trusts the language-length byte, allocates a variable-length stack array, and passes a potentially negative text length to `memcpy`. A one-byte payload declaring a 63-byte language code yields `textLength = -63`, converted to a huge unsigned copy length. The underlying decoder also reads declared lengths without checking the available message bytes and appends records without enforcing its four-record array limit. Therefore merely validating the resulting Text record is insufficient for damaged/malformed tags.

Fix: validate the raw NDEF envelope in the library, limit record count and every field length, then validate the Text status byte, encoding, language length, and maximum accepted URI length before copying into a fixed buffer.

**7. P1 — Network operations suspend card polling and reader recovery.**

Location: `esp32SpotifyAlexa_v5_tft_ndef.ino:219–269, 825–898`; `SpotifyClient.cpp:33–119, 194–229, 232–289, 340–369`.

RFID polling, HTTP handling, recovery, token refresh, device discovery, playback retries, JPEG downloads, and Telnet commands run synchronously in the same loop. During any network call or multi-second delay, new cards are not polled and the 30-second reader check cannot run. The installed secure client defaults to a 120-second TLS handshake timeout; the application does not set an explicit one. Nested retry layers can extend the interruption further. `delay()` allows background ESP32 tasks to run but does not run this sketch's RFID/web handlers.

Fix: queue scans for a single network worker or use a bounded state machine, keep SPI/display access serialized, configure connection/read/TLS deadlines, and replace retry sleeps with scheduled retries. Gate requests while Wi-Fi is unavailable. Bound work accepted per Telnet loop iteration too.

**8. P2 — The RFID recovery check cannot detect many failures in reading cards.**

Location: `esp32SpotifyAlexa_v5_tft_ndef.ino:48–85, 241–245, 586–606`.

Only VersionReg values 0x00 and 0xFF trigger recovery. A disabled antenna, damaged configuration, or failed RF communication can leave VersionReg readable, so status says ONLINE and recovery never runs. Every other byte is also accepted as a valid version despite the comment listing known values. This is a confirmed coverage gap, not proof the chip suffered one of these failures.

Fix: distinguish SPI communication health from card-read health. Track polling progress, successful UIDs, read/authentication errors and reset outcomes; inspect relevant configuration/antenna state. Use bounded recovery for actual error patterns. An empty field alone is not evidence of failure.

The installed adapter already calls `PCD_StopCrypto1()` in `tagPresent()`, so a missing call in the sketch is not established as the root cause. Its explicit `haltTag()` method is unused; define and test removal/re-presentation behavior instead of relying on a three-second delay as debounce.

**9. P1 — Refresh responses expose credentials through unauthenticated diagnostics.**

Location: `SpotifyClient.cpp:57`; `esp32SpotifyAlexa_v5_tft_ndef.ino:39, 390–392, 464–469`; `settings.h`; Python credential constants.

The complete token response, including the access token and any rotated refresh token, is logged into history and served over unauthenticated HTTP and Telnet. Remote control/token replacement/restart endpoints are also unauthenticated. Client secrets are embedded in both helper scripts and the firmware settings. Separately, `setInsecure()` disables server verification for token and API traffic, permitting an active network attacker to impersonate those endpoints.

Fix: never log token payloads, remove credential values from shareable source, protect administration/diagnostics, and use verified TLS with clock synchronization and maintained trust anchors. Rotate secrets if source or logs have been shared outside trusted access. Merely deleting the printed token prefix does not fix the full-response leak.

**10. P2 — HTTP responses without Content-Length are silently discarded.**

Location: `SpotifyClient.cpp:279–282`.

`getSize()` is -1 for unknown-length/chunked responses in the installed HTTPClient. The `> 0` guard discards those bodies while returning HTTP 200, causing device/album/current-track parsing to fail. HTTPClient's `getString()` has the decoding machinery needed for these responses.

Fix: consume response bodies based on response semantics, supporting unknown-length/chunked transfers with a size cap. Do not treat successful status plus an absent body as valid parsed data.

**11. P2 — Image download does not recognize complete, truncated, or chunked images.**

Location: `SpotifyClient.cpp:356–369`; `esp32SpotifyAlexa_v5_tft_ndef.ino:1024–1049`.

The raw socket loop ignores Content-Length and HTTP chunk framing. With a persistent connection it waits the full five seconds even after a complete image arrives. At the 64-KiB cap it also waits until the deadline, and any partial bytes are returned as success and cached before JPEG validation. A failed replacement download can overwrite the bytes associated with a previously cached image.

Fix: use a bounded HTTP-decoded sink, reject oversized/incomplete bodies, stop at completion, validate image metadata/JSON and JPEG decoding, and update cache state only on success. Do not let optional artwork delay the next scan.

**12. P2 — Token replacement destroys the working credential before validating the candidate.**

Location: `esp32SpotifyAlexa_v5_tft_ndef.ino:790–795`; `SpotifyClient.cpp:59–89`.

A typo submitted in the portal immediately replaces NVS and the client's token before Spotify accepts it. The previous working token is lost. FetchToken also accepts parseable JSON without verifying a nonempty access token/valid expiration and marks a 200 response as successful even after a JSON parse error, suppressing retries within that fetch.

Fix: validate the candidate using temporary state, preserve the existing credential on failure, and commit only a complete successful token response. Check NVS write results, especially on rotation.

**13. P2 — Rate-limit responses are retried without respecting Retry-After.**

Location: `SpotifyClient.cpp:194–229, 279–289`; `esp32SpotifyAlexa_v5_tft_ndef.ino:884–898`.

HTTP 429 follows the ordinary failure path; nested discovery/playback retries wait only two seconds and do not preserve the response's retry deadline. This can keep requests inside the blocked window. Spotify documents the intended handling: [rate limits](https://developer.spotify.com/documentation/web-api/concepts/rate-limits).

Fix: retain Retry-After, apply one shared cooldown for Spotify calls, and avoid retrying permanent 400/403 failures as if they were transport errors. Represent revoked credentials separately from transient connectivity errors.

**14. P2 — Common card formats and track links are not handled correctly.**

Location: `esp32SpotifyAlexa_v5_tft_ndef.ino:838, 855–862`; `SpotifyClient.cpp:163`.

The reader accepts only NDEF Text (`T`) records; normal URI (`U`) records are ignored. It accepts any `spotify:` prefix, but sends every non-artist URI as `context_uri`, including track links. Spotify requires tracks in the `uris` array and documents `position_ms` at the top level, whereas the code nests it inside `offset`: [playback request schema](https://developer.spotify.com/documentation/web-api/reference/start-a-users-playback).

Fix: either explicitly constrain card provisioning to supported Text album/playlist/artist records or decode URI records too. Validate object type/ID, construct JSON with a serializer, and choose the proper playback body for tracks versus contexts. Reject unsupported/localized URL shapes with a clear diagnostic.

**15. P2 — Playback controls report success and update local state after failures.**

Location: `esp32SpotifyAlexa_v5_tft_ndef.ino:295–320, 483–500, 533–537`.

Pause, volume, Next and transfer ignore Spotify's result, while handlers return success. Pause and volume start from fabricated defaults rather than the speaker's state. If the actual volume is 10%, the first +10 request sets it to 60%. Voice/phone actions and card playback also leave the local pause flag stale.

Fix: read actual playback/device state, honor volume support/restrictions, update cached state only on success, and propagate failures to the web UI. Dashboard JavaScript must check success before claiming a switch succeeded.

**16. P2 — The Python token updater can time out after a successful token installation.**

Location: `renew_token.py:53`; `esp32SpotifyAlexa_v5_tft_ndef.ino:794–805`.

The script waits eight seconds, but the endpoint refreshes the token and then does up to three device-discovery requests plus six seconds of deliberate delays before responding. With an unavailable Echo or slow network, the token can already be saved while the script reports it could not reach the ESP32.

Fix: acknowledge validated token installation independently of speaker discovery, expose pending/completed status, and give the client a compatible deadline. Both Python OAuth exchanges also lack request timeouts, status/error handling, and OAuth state validation; the older helper can print SUCCESS with a missing token. Add those checks and start the callback server before opening the browser.

**Additional review observations**

- `IsTokenValid()` reports only a stored flag, not expiration; dashboard authentication can appear valid long after expiry. Playback itself calls `EnsureTokenFresh()`, so this is primarily misleading diagnostics. The “force refresh” commands also only ensure freshness.
- CPU load measures wall time spent inside the loop's work, including blocking I/O/delays. It is not actual CPU utilization. Logs lack timestamps and reset reasons, making multi-day failures difficult to reconstruct.
- Memory churn exists in JSON, strings, log aggregation and artwork, but no monotonic application memory leak was demonstrated. The log limit is by message count, not total bytes. Bound response/log sizes and record minimum heap, largest free allocation, stack headroom and loop latency during a soak test.
- ArduinoJson 7.4.3 dynamically grows documents: the legacy `DynamicJsonDocument(512)` arguments are not hard memory limits in this build. Do not diagnose fixed-capacity exhaustion from those numbers without knowing the deployed version.
- Initialize both SPI chip-select pins as outputs/HIGH before initializing either peripheral. Currently RFID CS initialization occurs after initial TFT traffic. This is startup hardening; it does not by itself explain a two-day failure.
- After Wi-Fi reconnect, `MDNS.begin()` is called without restoring the HTTP service advertisement. Hostname resolution and service discovery are separate behaviors.
- Unused declarations/certificate data and pasted editing instructions remain in the sources. Cleanup is secondary to correcting state ownership, bounds and blocking behavior.
- No board profile, pinned dependency manifest, test suite or deployment history is supplied. Preserve these to make future regressions reproducible.

**Recommended fix order and hardware verification**

First fix the NFC dependency bounds, text-parser bounds, artist-state corruption, token boot precedence and speaker ownership. Then decouple scanning from network work, implement accurate recovery/error states, remove credential leaks and restore verified TLS.

After those changes, run at least a 72-hour hardware soak with timestamped serial logs. Exercise card removal during reads, repeated scans, tags left in place, malformed/multi-record tags, Wi-Fi loss and recovery, unavailable/reappearing Echo, token renewal followed by reboot, 401/429/5xx responses, partial/chunked/oversized images and a slow Telnet client. Record reset reason, loop/poll gap, free/minimum/largest heap, stack high-water mark, successful UID count, RFID read failures and recovery count.

When a scan fails, distinguish: no UID detected; UID detected but NDEF read failed; valid URI parsed but Spotify rejected playback; and playback accepted but Echo unavailable. VersionReg alone cannot make that distinction. If failures remain after software fixes, investigate reader power, wiring, shared-SPI signaling and supply transients with measurements.
