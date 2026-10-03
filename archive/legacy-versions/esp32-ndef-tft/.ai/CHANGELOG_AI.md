# AI change record

Append new dated entries. Keep intent, touched paths, trade-offs and validation; do not paste transcripts, tokens or routine tool output.

## 2026-09-20 — Retrospective baseline from pre-continuity sessions

This entry summarizes earlier work; it is not a claim that all changes occurred in one turn or that all features were hardware-verified together. See `review/FIXES.md` for expanded history.

### Reliability and networking
- Refactored `esp32SpotifyAlexa_v5_tft_ndef.ino` around hardware/network/main ownership and bounded queues; intent: Spotify/TLS waits must not stall RFID polling or cross-task SPI access.
- `SafeNdef.h`, `RfidReader.h`, `RfidPresence.h`, `RfidRecovery.h`: bounded parsing, capacity checks, held-card suppression and rate-limited recovery. User confirmed held-card/removal behavior; intermittent register corruption remains unresolved long-run risk.
- `SafeTlsClient.h`: decoded `udp_new_ip_type` assertion traced to Arduino core DNS/SNTP path; socket resolver preserves SNI/certificate validation without holding TCPIP locks during blocking operations. Host tests cover dispatch/failure behavior.
- `SpotifyClient.cpp`: explicit zero Content-Length for empty PUT/POST resolved411 behavior; token validation/persistence/cooldowns and bounded transport handling retained. Original client used `setInsecure()` and could return incomplete downloads; do not restore those behaviors.

### Device setup and UI
- `DeviceAuth.cpp`, `DeviceIdentity.cpp`, `OAuthSession.h`, `CertificatePolicy.h`: device HTTPS/PKCE reconnect, saved grant type, automatic leaf renewal. Intent: screenless/browser-based recovery without required Python helper. Private-CA browser trust remains a deployment constraint. User confirmed reconnect and subsequent refresh-token test.
- `WifiSetup.cpp`, `WifiSetupPolicy.h`: automatic offline AP/captive setup with displayed instructions, stable candidate validation, rollback, ongoing reconnect; automatic portal closes after stable restoration. Default Echo selection integrated into setup completion.
- `PlayerLogo.h`, `PlayerScreen.h`, `PlayerLanguage.h`: supplied mascot integrated; German default plus English/French/Spanish, status and URL retained. `assets/README.md` tracks asset provenance.
- `DisplaySettings.h`: persisted live TFT debug log mode and cover duration; cover expiry now shows startup screen30min, then blanks. Screenless mode supported.
- `Dashboard.h`: responsive Player/Settings/Diagnostics navigation, embedded logo, compact status, explicit speaker discovery, on-demand logs with pause/copy. Intent: everyday controls first, Wi-Fi and recovery controls out of the way. Browser mock previews + host tests passed.

### Artwork regression investigation and resolution
- Original sketch: static64KB JPEG buffer, image index1, native-size draw `(0,-30)`, retained cache. New allocations plus additional services reduced contiguous heap/headroom.
- First adaptive download buffer caused local413; larger pre-TLS allocation then caused fast-1 connection failure. Allocation moved after HTTPS handshake; confirmed that alone did not preserve image quality.
- User measured300px JPEG49,457B vs32,148B budget, then55,647B vs~32KB. **User rejected64px fallback**, now excluded in `Artwork.h`.
- Initial `ArtworkSpool.h` downloaded to flash but copied entire JPEG into RAM afterward; still failed. Replaced with direct `JPEGDecoder::decodeFsFile(fs::File)` and explicit queue/file ownership. Filesystem initialized before GET; autoformat limited to wholly erased partitions.
- `CoverLayout.h`: bounded scanline scaling to320×240 with centered aspect-preserving crop; no full-screen framebuffer. Large RAM JPEG caches released after drawing to preserve future TLS headroom; Last cover may re-download.
- **Device verification succeeded**: user confirmed300px46,212B cover via flash; later57,796B cover also displayed. Tests cover quality selection, transport bounds, scratch-file lifecycle/ownership, and geometry. Firmware compile is not a substitute for this device evidence.

### Startup readiness
- `StartupPolicy.h`, sketch worker scheduling: avoid starting a30s maintenance wait before Wi-Fi/clock readiness; check invalid token state every1s with cooldowns. Keep early play pending without consuming retry budget; latest play/select supersedes pending intent.
- User reported refresh improved; logs showed~3–6s after boot. A specific successful card-before-auth test is still pending.

### Immediate loading / error feedback
- Added `LoadingScreen.h`, `LoadingState.h`; updated sketch and `tests/test_loading.cpp`.
- Animated storybook begins after new UID detection, before NDEF parsing/API calls; small-region redraw every120ms. Covers replace loader after download/validation. No added network asset or framebuffer.
- Distinct localized errors for unsupported/read-failed cards, playback, artwork and queue-full states;5s error dwell,90s loader visual timeout. Setup/debug priority preserved.
- Job IDs/generation checks prevent stale results replacing new-card feedback; JPEG decode stops when polling detects a newer presentation.
- Full host suite passed; TFT/headless compiled. **Device visual/timing validation pending**.

### Cover duration in minutes
- `Dashboard.h`, `tests/test_dashboard.js`, `README.md`: minute-based input, fractional minutes,0=always-on, default10min, max1440min. Convert at UI boundary; preserve seconds in API/NVS and old saved values.
- Why: easier user-facing duration entry without a storage migration or breaking API compatibility.
- Dashboard tests passed; TFT build1,519,492B (77%). No hardware confirmation yet; latest headless build predates this HTML-only edit.

## 2026-09-20 — Establish mandatory continuity workflow
- Added root `AGENTS.md` requiring context reads and completion updates for significant work.
- Created `.ai/ARCHITECTURE.md`: stack, component/owner map, persistence, runtime behavior, ADRs and build/test entry points.
- Created `.ai/SCRATCHPAD.md`: active state, ordered next steps, validation ledger, unresolved reliability issues and operational gotchas.
- Created this append-only `.ai/CHANGELOG_AI.md`; prior history is explicitly retrospective.
- Intent: future sessions must recover rationale and verification boundaries without chat history; avoid repeating failed buffer/thumbnail/TLS approaches.
- Validation: checked references against current source/configuration; no secrets copied; documentation-only task, no firmware edits or rebuild.


## 2026-09-20 — Two-hour positive device report
- User reports approximately two hours of very good operation and very fast card pickup.
- Recorded in `.ai/SCRATCHPAD.md` as qualitative hardware evidence. Exact firmware build, reset count, heap trend and timing measurements were not supplied; do not infer validation of every feature/edge case.
- Preserve current working implementation; longer soak and specific pending checks remain. Architecture unchanged; no firmware edits or tests/builds needed for this evidence-only update.
