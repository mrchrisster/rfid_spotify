# AI change record

Append new dated entries. Keep intent, touched paths, trade-offs and validation; do not paste transcripts, tokens or routine tool output.

## 2026-09-20 — Retrospective baseline from pre-continuity sessions

This entry summarizes earlier work; it is not a claim that all changes occurred in one turn or that all features were hardware-verified together. See `review/FIXES.md` for expanded history.

### Reliability and networking
- Refactored `rfid_spotify.ino` around hardware/network/main ownership and bounded queues; intent: Spotify/TLS waits must not stall RFID polling or cross-task SPI access.
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

## 2026-09-20 — Show / episode cards
- Cause: `SafeNdef.h` allowed only album/artist/playlist/track, rejecting otherwise valid `/show/<id>` tags before playback. Added show/episode normalization and NDEF Text/URI regression coverage.
- `SpotifyClient.{h,cpp}` resolves a show to the first playable episode in API order (one-entry pages, cap10; keep24KB JSON limit). Avoid undocumented show context; use episode `uris`, position0. Also fixes direct URL Play callers using unnormalized payload. No resume/chronological/whole-show semantics promised. Episode playback still requires live Echo validation.
- Sketch resolves before Play, skips shuffle for episodes, retains episode URI for covers; playback reads include episodes. `Artwork.h` supports direct episode metadata and stale-playback identity checks. Existing full-quality flash rendering unchanged.
- Tests: `python3 tests/run_tests.py` passed (including show/episode NDEF, playback payload, unavailable/malformed/API-error/bounded-scan and episode artwork checks). Original ESP32 min_spiffs TFT build1,521,904B/77%, globals63,528B; headless1,393,936B/70%, globals58,000B. No upload or live Spotify/Echo test performed.
- Updated README, architecture decisionA12 and scratchpad; next: flash and scan show + episode cards, verify resolved-episode log, successful playback and cover.

## 2026-09-20 — Bookmark request / interaction clarification pending
- User requests resume audiobooks, but repeat presentation of same card should restart. Asked whether A→B→A resumes and A→remove→A restarts; awaiting answer before implementing dependent behavior.
- Read current playback/worker/presence and official Spotify playback-state/start reference. Resume requires saved item+progress, album/playlist offset URI and selected-speaker validation. No firmware edits/builds this turn; architecture unchanged. Pending design/checks recorded in scratchpad.

## 2026-09-20 — Audiobook format clarification
- User asks whether audiobooks are albums or shows/episodes before deciding resume/restart interaction. Spotify also has dedicated audiobook/chapter catalog types; current firmware does not accept audiobook links. No evidence for which format is numerically most common.
- Recommended bookmark model spans supported formats: original card target + resolved book/context + item URI + progress. Artist card must retain selected album to resume instead of drawing a different album. Interaction remains pending; no firmware edits or tests needed for this clarification.

## 2026-09-20 — Persistent resume + quick-repeat restart
- User accepted ordinary scan resumes, remove/re-present same card within10s restarts; later return/reboot resumes. Implemented window measured from prior presentation, requiring existing healthy1.5s removal latch. Invalid reads/queue rejection clear gesture; held cards remain suppressed.
- Added `CardRestart.h`, `PlaybackBookmarks.h`: hardware gesture vs network-owned16-slot local bookmark store; original target URI→resolved context/item/ms. Album/playlist resumes saved track; artist retains selected album, show retains selected episode. Device/content/position validation prevents unrelated progress overwrite. Missing saved content surfaces failure without erasing it.
- Worker captures actual playback before switches and pause/next/speaker controls; idle samples30s, dirty NVS saves60s, immediate commit on successful playback.5s guard reduces stale Connect snapshots; per-job frozen plans retain restart/resume intent across retries. Save failure retries; no initialized namespace means RAM-only with warning. Flash wear vs abrupt-loss replay explicitly documented.
- `SpotifyClient::Play` accepts bounded position and validated album/playlist item offset; single episodes/tracks use URI list. Existing immediate loader and full-quality cover paths retained.
- Verification: full `python3 tests/run_tests.py` passed, including new persistence/reboot/write-failure/eviction/provenance/gesture/rollover and resume-payload tests. ESP32 min_spiffs TFT1,530,616B (77%), globals66,104B; headless1,402,696B (71%), globals60,584B. No hardware upload or real Echo test.
- Updated README and architecture A13; scratchpad contains required A→B→A, repeat/reboot/artist/episode and latency checks. Limitations:16 local targets, no imported history/completion detection/audiobook links; added API traffic and capture latency; checkpoint freshness can exceed1min during stalls.

## 2026-09-20 — Extend repeat-card restart window to10minutes
- User explicitly rejected10s as too short to decide whether to restart. `CardRestart.h` now uses named600000ms window from previous presentation; same UID/content required, intervening card clears matching, held-card/removal behavior unchanged.
- Updated README, architecture intent and scratchpad. Historical entries retain original10s decision as history.
- Targeted `tests/test_bookmarks.cpp` passed with ASan/UBSan, including restart after1min, inclusive10min boundary, expiry just beyond10min, existing persistence/provenance/rollover cases. No firmware rebuild/upload; hardware verification pending.

## 2026-09-20 — Set repeat-card restart window to2minutes
- User revised timeout from10min to2min. `CardRestart::WindowMs=120000`; elapsed time remains measured from previous presentation. README/current architecture/scratchpad updated; cover display duration unchanged.
- Targeted bookmark/gesture tests passed with ASan/UBSan, including1min restart, inclusive120000ms boundary and120001ms expiry. No firmware rebuild/upload; device verification pending.

## 2026-09-21 — Audit Echo failure indication
- Confirmed generic5s TFT error for final failed card playback while its loader remains active; bounded retries for transient cases. Web UI shows failures for its submitted commands/logs. No proactive/persistent speaker health indicator; token-ready does not mean Echo available; bookmark poll failures don't raise UI alerts.
- Documented limitations and optional follow-up in architecture/scratchpad. Code inspection only; no firmware edits/builds/device test.

## 2026-09-21 — Persistent Echo availability warning
- User approved separate speaker health and persistent outage indication. Added `SpeakerHealth.h`: Unknown/Available/Playing/Paused/Unavailable; two consecutive successful discovery misses trigger outage, uncertain API results do not, and confirmed outage persists until positive recovery.
- Network-worker idle checks every30s (playback, then discovery if needed), auth/Wi-Fi/cooldown aware; state-change logs, atomic status and last successful check age exposed via status API. Release playback JSON before discovery TLS to preserve heap. No sound detection or automatic audio restart.
- Hardware-owned localized bottom warning band persists beyond loading/cover timeouts; Wi-Fi setup/debug priority preserved. Recovery restores startup/active loader. Dashboard gets independent speaker badge with existing attention styling.
- Native/Python regressions passed, including new health-state tests; initial dashboard test exposed unsupported fake-DOM inline style usage, replaced with existing badge helper. Updated dashboard tests passed for all five states; Wi-Fi portal tests passed. Final firmware compile results recorded in scratchpad; no upload/device validation.
- README, architecture A14 and scratchpad updated with device outage/recovery, paused/unknown and timing/priority limits.
- Final builds passed: TFT1,533,772B/78% (globals66,112B), headless1,406,156B/71% (globals60,592B), original ESP32 min_spiffs.

## 2026-09-21 — Wireless flashing inquiry
- Verified firmware has no OTA handler; `/update` aliases Spotify token replacement. Installed ESP32 core min_spiffs provides app0/app1 each1,966,080B; current TFT1,533,772B fits. Recommend local web firmware uploader, enabled by one initial USB upload; no server/subscription required.
- Explained prospective browser workflow using exported application binary and retained NVS. No implementation/build/upload performed; scratchpad records pending option, architecture already states OTA absent.

## 2026-09-21 — Browser OTA firmware uploader
- Added main-owned `FirmwareUpdate.h` and shared/pure-policy `FirmwareUpdateState.h`: prepare/status/multipart-upload routes,128-bit session, inactive app-slot streaming via Update, exact size/header/final SDK validation, abort/60s inactivity timeout,3s delayed reboot. No full-image RAM buffer, bootloader/partition writes or credential/FS erasure. Existing `/update` token alias retained.
- Worker safe-point acknowledgments avoid suspending task-held resources. Network waits for bookmark/token persistence; hardware shows progress and pauses card reads; main drains results but suspends Wi-Fi setup/certificate ticks. Other main web mutations blocked during update. Failure resumes workers; no automatic rollback of a valid-but-broken new application.
- Settings uploader: local application file selection, first-header check, preparation polling, progress, server completion/error handling. Existing LAN auth policy retained; target screen/private identity remains operator responsibility. Initial USB installation required.
- Verification: full native ASan/UBSan + Python + JS suite passed with streaming/header/fragment/truncation/oversize/session/quiescence/write/validation/abort/timeout tests; added browser upload tests then Node dashboard passed. Final ESP32 min_spiffs TFT1,547,940B/78%, globals66,432B; headless1,420,196B/72%, globals60,912B. Header policy tested against actual generated artifacts for both variants: app accepted, merged/bootloader/partitions rejected.
- README procedure, architecture A15 and scratchpad updated. No device USB/OTA upload performed; first real update/interruption/recovery test pending. Generated binaries contain credentials and remain in temporary build directories; not published.

## 2026-09-21 — Larger web logo and OTA variant clarity
- `Dashboard.h`: logo54×56→80×84px desktop /68×71px phone; existing embedded PNG retained. Firmware Settings now states installed TFT/headless variant and that selected binary determines target variant. No runtime conversion or cross-variant rejection claimed.
- README documents `PLAYER_HAS_DISPLAY=1/0`, same board/partition settings, separately named app binaries, per-device identity and initial USB requirement. Architecture/scratchpad updated.
- Node dashboard tests passed incl both variant labels. ESP32 min_spiffs builds passed: TFT1,548,372B/78%, globals66,432B; headless1,420,644B/72%, globals60,912B. No upload or browser/device visual verification.

## 2026-09-21 — First successful on-device wireless update
- User reports OTA “worked flawlessly” after guidance to select `rfid_spotify.ino.bin`. Normal browser update path now user-confirmed on hardware.
- Recorded evidence in architecture/scratchpad. No exact build/variant or individual persistence checks supplied; interruption/power-loss recovery and other-device variant remain unconfirmed.
- Documentation only; no firmware edits, rebuild or tests required.

## 2026-09-21 — Feature ideas / Bluetooth feasibility
- Discussed potential sleep timer, card library/bookmark controls, volume cap, configurable pause-on-removal and listening progress. Suggestions only, not accepted requirements or new implementations.
- Verified official ESP32 A2DP transmit/receive support; Spotify eSDK hardware partner path restricted to organizations; unofficial cspot targets ESP32 but no compatibility/reliability test performed. Current player has no audio stream. Recommended retaining controller/Echo architecture, optionally pairing Echo to Bluetooth speaker using Amazon's documented flow.
- Updated scratchpad; architecture remains unchanged. No firmware edits/tests/builds. Sources: Espressif esp_a2dp API; Spotify commercial-hardware docs; github.com/feelfreelinux/cspot; Amazon help GG8S76D3BYTGC424.

## 2026-09-21 — Persistent card library and cover height fit
- `PlaybackBookmarks.h`: reuse16 bounded URI slots for library; optional separate change-only `lNN` labels avoid rewriting names at every progress checkpoint. Legacy `bNN` records compatible. Active-slot protection, unplayed rows excluded from resume plans, UTF-8 bounded labels.
- `CardLibraryMetadata.h`: reuse artwork metadata; stale playback provenance checks; album/artist or episode/publisher names. `rfid_spotify.ino`: network-owned snapshot→main cache, GET library read-only, validated POST replay through existing bookmark/Spotify queue; ordered TFT loading/error path. Headless fetches metadata after play. Missing title falls back to URI.
- `Dashboard.h`: recent cards + Play on Player; safe text rendering, no refresh side effects; Settings cover layout. `DisplaySettings.h`/`CoverLayout.h`: persistent height-fit flag, square240×240 centered versus legacy fill/crop, no framebuffer or image-quality reduction; applies next draw.
- Wear discussion: NVS distributes erases; SPIFFS temporary covers still rewritten and deleted. Cloud `resume_point` possible for episodes/chapters with extra OAuth scope but not album bookmarks; no OAuth/resume behavior change made. User mostly uses albums; recommended5min periodic saves (80% fewer vs60s), plus transitions, retaining30s RAM observations. **Not implemented pending acceptance of larger power-loss replay window.**
- Validation: full `python3 tests/run_tests.py` passed (native ASan/UBSan, Python, Node). New library persistence/no-redundant-label-write/provenance/eviction/UTF-8 and UI play/no-on-load-command checks; cover geometry/flags and UI settings tested. ESP32 min_spiffs builds passed: TFT1,559,076B/79%, globals69,008B; headless1,431,660B/72%, globals63,488B. No upload, on-device tests or browser visual inspection. README/architecture/scratchpad updated; logs in `/private/tmp/library-{tests,tft,headless}.log` ephemeral.

## 2026-09-21 — Accepted five-minute bookmark checkpoint policy
- User accepted recommendation after clarifying most cards use albums. `BookmarkPolicy.h` separates30s in-RAM Spotify position sampling from300s persistence (previously60s); about80% fewer periodic save opportunities. Changed-only writes and transition checkpoints retained. Idle saves also flush last-known RAM state when disconnected. Sudden power loss may replay~5min or more during API/worker stalls.
- `rfid_spotify.ino`: replaced immediate HTTP restart with queued network-owned capture/save, token-persistence check, failure cancellation507, and main reboot after success. Blocks new mutations/card submissions while pending. Queue-full restores normal operation. Firmware preparation attempts fresh capture once before persistence acknowledgment; storage retries do not repeatedly request position.
- `Dashboard.h`: restart completion/cancellation feedback. `tests/test_bookmarks.cpp`:5min threshold, rollover,24 saves/2h, unchanged position no-write; dashboard tests cover restart results. README and context files document trade-off and hardware test gaps.
- Full `python3 tests/run_tests.py` passed. Both ESP32 min_spiffs builds passed: TFT1,560,156B/79%, globals69,016B; headless1,432,776B/72%, globals63,496B. No hardware upload/verification. Temporary logs `/private/tmp/checkpoint-{tests,tft,headless}.log`; firmware outputs remain private in existing `/private/tmp/player-wifi-{tft,headless}` build directories.

## 2026-09-21 — Continuity documentation cleanup
- User requested `.ai` refresh. Rechecked `BookmarkPolicy.h`, `PlaybackBookmarks.h`, `DisplaySettings.h` and sketch library/settings/restart/OTA paths against current docs.
- `.ai/SCRATCHPAD.md`: replaced accumulated historical active-task bullets and conflicting build claims with one current handoff, prioritized device checks, current validation ledger and latest private build/log paths. Preserved unresolved hardware risks and evidence boundaries; historical change entries remain intact here.
- `.ai/ARCHITECTURE.md`: added missing policy/OTA/speaker ownership rows; corrected bookmark RAM estimate after label expansion, active-entry eviction and library-before-play semantics; marked library-only build sizes superseded by A17. Five-minute saves, separate label writes and unimplemented cloud-resume/cover-cache options remain explicit.
- Documentation only; inspected source and existing validation evidence. No firmware modifications, fresh tests/builds, upload or device validation performed.

## 2026-09-21 — Show series labels and artist Shuffle album
- User identified bad “Vorspann” row as `spotify:show:6Tw6DQgHdXayMDBlkMBpAw`; metadata previously used selected episode title. `CardLibraryMetadata.h` selects parent show name/publisher for show cards, verifies provided parent URI, preserves explicit-episode and album names. Post-play metadata refresh now repairs existing cached labels after Play/rescan, including headless.
- Extracted existing shuffled catalog selection into `ArtistAlbumSelection.h` with injected client/RNG for HTTP tests. Added bounded exclusion of saved album and wraparound, preserving historical catalog/artist restrictions. No alternative within≤10 candidates returns422 without new playback.
- `Dashboard.h` artist-only Shuffle album; optional validated `/api/library/play` shuffle boolean; worker membership/type guards, zero-offset fresh album plan frozen across retries. Chapter order retained; successful playback replaces artist card's bookmark and clears stale title; failures preserve current bookmark. `Reliability.h` adds order size/rewind; `PlaybackBookmarks.h` clears changed-context labels only on successful start. Normal scan/Play resume unchanged.
- Full `python3 tests/run_tests.py` passed (ASan/UBSan, Python, Node); new coverage for show-name cache repair, wrong-parent rejection, explicit episodes, changed-context labels, candidate exclusion/wrap/API failure/single-album behavior and artist-only UI requests. Both min_spiffs builds passed: TFT1,562,648B/79%, globals69,016B; headless1,435,024B/72%, globals63,496B.
- README/all `.ai` files updated with intent, limitations and pending checks. No firmware upload, device Spotify/Echo test or browser visual inspection. Ephemeral logs `/private/tmp/shuffle-{tests,tft,headless}.log`; private binaries remain in existing temporary build directories.

## 2026-09-21 — Repeat-scan shuffle work paused for confirmation
- Began artist repeat-scan→existing album-shuffle path; pure gesture policy tests added, worker physical-shuffle guard changed, log/UI text updated. Artist-only stage full suite passed. Then temporarily removed restart flag for fixed targets using proposed default resume; final regression/build commands started, results not collected yet.
- User clarified first scan resume, second another album, show/episode unchanged; explicitly requested behavior confirmation before further work. Paused code work; no upload. Pending scope: include fixed-album cards via their artist, preserve prior show/episode repeat restart? Current interim source/docs must be reconciled after answer. Scratchpad/architecture record unfinalized state to prevent accidental handoff/flash.

## 2026-09-21 — Confirmed artist/album repeat shuffle; held-card semantics
- User confirmed proposed semantics (including fixed album cards) and asked about a held card. Answer: one trigger while present, no timer-triggered action;≥1.5s healthy absence before re-presentation;120s window measured from previous scan, so removal after holding>2min then re-scan resumes.
- Reconciled interrupted draft: restored `Job.restart` for show/episode/track/playlist repeat behavior; `CardRestart::shuffles` maps repeated artist/album URIs to shared shuffle job. Worker allows physical album shuffle, ordinary scans/library Play still resume. No changes to RFID presence latch or wiring.
- `ArtistAlbumSelection::chooseForCard`: fixed album uses original album first-track lookup then track album-level primary artist; rejects mismatched/relinked album and malformed artist. Two bounded responses avoid full audiobook album JSON overflow and artist drift from later chosen albums. Existing selection excludes saved album and freezes plan across retries; failed selection does not erase old bookmark.
- README/dashboard/current architecture/scratchpad reconciled. Full ASan/UBSan + Python + Node suite passed, including album-primary-artist vs guest track artist, mismatch rejection, gesture boundaries/type mapping, prolonged held UID suppression. TFT build1,564,860B/79%, globals69,016B; headless1,437,272B/73%, globals63,496B. No upload, live Spotify/Echo or new hardware verification. Logs `/private/tmp/card-gesture-{tests,tft,headless}.log`; private binaries retained in existing temp build directories.

## 2026-09-21 — Reconnect troubleshooting awaiting browser error
- User reports browser reconnect not working on TFT build Sep21 14:09:13. Supplied logs only show transient speaker Unknown followed by successful token refresh and Available; no browser OAuth completion/error evidence.
- Reviewed current `DeviceAuth.cpp` handlers and worker integration. Explained ordinary token refresh is separate from `[Auth] Browser reconnect saved`; requested exact browser error/stage without secrets. No root cause established, speculative fix or certificate/auth-policy change made.
- Scratchpad updated; architecture unchanged. Code inspection/documentation only; no new tests/builds/upload/device interaction.

## 2026-09-21 — Browser reconnect survives cooldown / bounded connection retries
- New supplied logs establish browser failures -1 then429; later refresh proves old connection remains usable. Found local cooldown consumed/discarded one-time queued OAuth code. Original connection failure remains undiagnosed; no hardware/network fix claim.
- `DeviceAuth.{h,cpp}` retains queued code, waits for Wi-Fi/clock/cooldown, limits attempts3/deadline90s, retries only pre-send -1 and429, improves result-page feedback. Queue peek avoids another permanent~1KB heap allocation. Network worker prioritizes pending reconnect over routine Spotify calls. `SpotifyClient.cpp` emits safe numeric DNS/TLS/heap diagnostics.
- Added fake queue peek and real-handler tests for initial cooldown, -1 retry/success, remote429, exhausted attempts preserving old token, terminal400/-11 and timeout without HTTP exchange. Full `python3 tests/run_tests.py` passed; both ESP32/min_spiffs builds passed: TFT1,567,364B/79%, globals69,016B; headless1,439,864B/73%, globals63,496B. No upload/live browser/device verification.
- README and all `.ai` files updated. Next: flash appropriate app binary, reconnect once/keep result open, verify saved token including reboot; collect safe transport diagnostics on repeat failure. Private binaries remain in temporary build directories; logs `/private/tmp/reconnect-{tests,tft,headless}.log`.

## 2026-09-21 — Reconnect TLS allocation failure: suspend browser HTTPS during exchange
- New device logs establish X509 allocation failure on first attempt; subsequent X509 fatal/public-key parse errors. Existing startup refresh succeeds. Retries alone did not resolve; avoid concurrent inbound/outbound TLS while retaining all verification.
- `DeviceAuth.cpp`: send waiting page before requesting main-task server stop; worker waits for successful shutdown acknowledgment. Server remains stopped through bounded retries, restarts on terminal result. Polling fetch catches temporary outage, waits for completion header, then navigates. No credential/PKCE/session policy changes or additional permanent exchange buffer.
- `tests/test_device_auth.cpp` covers gate before server stop, failed-stop block and restoration plus existing retry/auth validation. Added `tests/test_reconnect_poll.js`, registered in `tests/run_tests.py`, verifies offline/pending/error responses do not navigate and terminal response does. Full existing suite passed; new JS test passed separately. Both builds passed: TFT1,567,924B/79%, globals69,024B; headless1,440,416B/73%, globals63,504B. Logs `/private/tmp/reconnect-memory-{tests,tft,headless}.log`.
- README/all `.ai` docs updated. Not uploaded; actual reconnect and memory recovery need device verification. Next upload correct app binary; keep result page open, look for HTTPS pause→exchange→saved→HTTPS restart; test saved token after reboot.

## 2026-09-21 — User verifies reconnect TLS serialization
- User browser success + logs confirm HTTPS suspension, first-attempt OAuth saved and HTTPS restoration. Free heap136508B/largest49140B versus preceding failed attempt76088B/34804B; exchange~571ms. Same cached build banner15:21:59, new pause log confirms changed path.
- Later Speaker Unknown alone is inconclusive. Inspected `checkSpeaker` and `SpeakerHealth.h`: API/parse/network/auth uncertainty or first successful discovery miss can yield Unknown; two discovery misses required for Unavailable. Next: saved-token test after reboot, card playback and subsequent status.
- Updated all `.ai` context files; documentation-only, no firmware change/rebuild or assistant device interaction. Single-run success does not establish soak reliability.

## 2026-09-21 — Post-reconnect discovery failure/recovery; release idle HTTPS
- User logs discovery-1/429/-1 after reconnect, then Available before card. Follow-up confirms resumed playback204, full-quality54099B cover, Playing and discovery200. Recovery precedes this code change; exact failed-call TLS errors unavailable.
- `DeviceAuth.cpp` route wrappers send complete response with Connection:close then return failure to close TLS transport; stops idle result page retaining session memory. Browser cookie/state retained; OAuth suspension unchanged. `tests/fakes/https/esp_https_server.h` records actual registered handlers; `tests/test_device_auth.cpp` covers close contract and intact successful result. `rfid_spotify.ino` retains last speaker list on failure, replaces on success (including empty list).
- Full suite passed; TFT1,568,052B/79%, globals69,024B; headless1,440,532B/73%, globals63,504B. Logs `/private/tmp/speaker-tls-{tests,tft,headless}.log`. Cache behavior inspected/compiled, not separately unit-tested. Docs updated; no upload/new-device verification. Next: reconnect with page left open then discovery, saved-refresh-token test after reboot.

## 2026-09-21 — Album cards always shuffle; unattended OTA design
- Album URI first/repeat/expired scan and library Play choose another album at0; held-card latch unaffected. Artist/show/episode policies unchanged. Suppress album progress capture/checkpoints, retain last selection for exclusion and labels; old progress ignored without erasing data.
- Changed `CardRestart.h`, sketch worker, `PlaybackBookmarks.h`, dashboard/README. Full suite passed incl first/expired gestures and no-progress-write regression; TFT1,568,276B/79%, globals69,024B; headless1,440,712B/73%, globals63,504B. No upload/device validation; logs `/private/tmp/album-always-{tests,tft,headless}.log`.
- User then requested unattended update method. Inspected current OTA and pinned Arduino core; wrote `.ai/AUTO_UPDATE_PLAN.md`, proposal only. Bootloader rollback enabled in local build but default Arduino verification accepts immediately; deferred local health confirmation required. No release publication, hosting, scheduler or automatic flashing enabled.

## 2026-09-21 — Restore resume and remove album/artist repeat timeout
- User reversed always-shuffle request. Restored album capture/checkpoint/resume and library Play behavior; same physical artist/album card consecutively re-presented now shuffles regardless of elapsed time. Different card or reboot resets sequence, held-card behavior unchanged. Show/episode/track/playlist retain2min restart window.
- Updated `CardRestart.h`, `PlaybackBookmarks.h`, sketch worker, dashboard/README and `.ai` docs. Host tests include24h repeat, A→B→A, reset, show/episode window, album item/position persistence and unchanged no-write. Full suite and both builds passed: TFT1,568,100B/79%, globals69,024B; headless1,440,584B/73%, globals63,504B. Logs `/private/tmp/no-timeout-{tests,tft,headless}.log`. No upload/device verification. Auto-update remains proposal only.

## 2026-09-21 — Compile and wireless flash completed
- User explicitly requested compile/flash. Recompiled current TFT with ESP32/min_spiffs; sketch1568100B/79%, globals69024B; app1568256B. No USB port present, existing TFT device found over LAN192.168.0.75. Used `/api/firmware/prepare`, ready acknowledgment, then multipart upload to inactive slot; server confirmed installed/restarting.
- Verified running build `Sep 21 2026 17:26:21`, uptime25s, Wi-Fi connected, token valid/saved/not revoked, RFID ready/version92, speaker Playing, HTTPS ready. No assistant playback command or physical scan. Latest gesture behavior still requires user card test; no long-soak claim.
- Build log `/private/tmp/player-flash-build.log`; private app remains `/private/tmp/player-wifi-tft/rfid_spotify.ino.bin`, not published. Removed temporary full preflash status file (contained configuration). No firmware code changes this turn; existing passing regression results retained. Context docs updated.

## 2026-09-21 — User confirms deployed repeat-card behavior
- User reported “that worked” following requested physical repeat-card test after OTA to TFT build Sep21 17:26:21. Updated scratchpad validation ledger and A24 evidence; marked A23 explicitly reverted and reconciled stale timeout/upload notes.
- Current contract: artist/album first scan resumes; consecutive same physical card re-presentation shuffles without timeout; A→B→A/reboot/library Play resume; held card triggers once. Show/episode restart timer remains2min. Specific tested card types/time interval not supplied, so expanded scenario matrix/soak remains pending.
- Documentation only; no rebuild, firmware change, upload or additional device operation. Previous passing tests/builds and deployment evidence retained. Auto-update remains proposal only.

## 2026-09-30 — Investigate audiobook stopping after first part
- Read player `/api/logs` (plain text, not JSON), sanitized `/api/status` and `/api/library` over LAN. No commands/refresh/playback mutation. Device running Sep21 17:26:21 uptime~4.7days; token/RFID healthy. Last scanned Latte Igel show281IWghdu0yxOtNWFm4F6S job40 accepted204/resume73462ms, cover displayed, later Paused/Available; preceding Olchis artist playback also accepted.
- Source confirms show→single episode or saved episode, single-element uris request, no next-episode continuation. Album requests retain full context. Logs do not identify exact episode number or Spotify stop cause; distinguish implementation limitation from proven natural-end event. Documented finding/next-feature constraints; no code change/build/flash. Existing48-line log retention limits retrospective detail.

## 2026-09-30 — Implement show continuation via remaining episode list
- User confirmed first part ended~5min; manually starting part2 continued through show. Added `ShowPlayback.h` bounded plan (pages5,max100entries,45s between-call deadline), full validation before playback, API-order playable URI list starting from saved episode+position. No pause-triggered next or append-queue retries; errors never silently truncate. Public `SpotifyClient::Play(show)` and sketch worker use plan, worker freezes retries and disables shuffle, releases plan heap on terminal/superseding intent.
- Show bookmarks now follow observed same-show episodes on selected Echo; persist episode+position using existing wear policy. Explicit episodes/artist/album gestures untouched. README/all `.ai` context files updated with reasons, bounds and API compatibility caveat: official playback docs describe track lists; multi-episode Echo acceptance requires live test.
- Full suite passed incl sequence/pagination/resume/unavailable/malformed/duplicate/oversize/missing bookmark/frozen replay, later-episode persistence and wrong-show rejection. Both final builds passed: TFT1,575,320B/80%, globals69,024B; headless1,447,792B/73%, globals63,504B. No upload or live playback mutation. Logs `/private/tmp/show-sequence-{tests,tft,headless}.log`.

## 2026-09-30 — Flash show-continuation firmware
- User explicitly requested flash. Verified LAN target192.168.0.75 TFT previously Sep21 17:26:21; uploaded compiled display app1575472B via existing prepare/ready/multipart OTA. Server confirmed installed/restarting; no credential output/publication.
- Post-reboot status confirms Sep30 11:17:36, uptime6s, TFT=true, Wi-Fi connected, token valid/saved, RFID ready/version92, HTTPS ready, speaker Available. SHA256 fc65a15ce13fabf4d3e3a074d809bed1db2a929063bb9aa5aae52c45e6cd8a27. Used prior passing suite/builds; no code changes/rebuild needed for upload.
- No playback command sent. User test needed: Latte Igel sequence and natural part1→part2 progression, pause/resume/later-part bookmarks. Context docs updated; firmware deployed does not itself prove multi-episode API compatibility.

## 2026-10-03 — Prepare GitHub root project and legacy archive
- Cloned existing history at50d7b37; current local firmware promoted to root as rfid_spotify.ino with current docs/assets/tests/tools. Preserved all prior implementations under archive/legacy-versions, original README/media, and both case-colliding READMEs with distinct names.
- Omitted36 private provisioning paths already tracked upstream; sanitized legacy config literals. Extended ignore rules and added SECURITY.md describing historical exposure/rotation gap. Audited current local credential values absent from publication tree. No history rewrite, credential rotation, device reset or flash.
- Reorganized checkout full regression suite passed; placeholder TFT/headless build and final push results follow. GitHub SSH authentication available; no HTTPS Git credentials or gh CLI installed.

- Publication validation complete: full regression suite passed; clean root rfid_spotify sketch compiled with placeholder secrets and no DeviceCertificate in TFT/headless variants (1547352B/78%,1419592B/72%; globals68560B/63040B). Tracked-file audit: no private artifacts; current local credential values absent. Logs `/private/tmp/repo-restructure-{tests,tft,headless}.log`. Runtime firmware source unchanged except sketch basename; no device flashed.

- Publication complete: restructuring commit `fa54ce3` pushed normally to GitHub main, preserving history. Persistent checkout `/Users/chrishelms/code/esp32SpotifyAlexa_v5_tft_ndef/github/rfid_spotify`; original hardware workspace remains separate and ignores github/. Follow-up continuity update records publication; no runtime changes or additional build required. Historical credential rotation and live show-continuation verification remain pending.

## 2026-10-03 — Arduino dependency installation and OTA discoverability
- README now lists six exact library names/authors/tested versions/purposes, board core3.3.11 setup/index, transitive Adafruit dependencies, bundled core libraries, headless differences and setup troubleshooting. Added setup navigation to existing OTA/variant instructions and explicit manual browser OTA/no unattended update distinction.
- Verified against sketch.yaml, current source includes, installed library.properties and official Arduino/Espressif installation guides. Documentation only; no firmware rebuild, test run or device upload. Existing pending hardware/security follow-ups unchanged.

## 2026-10-03 — Fix archived filename rejected by Arduino IDE
- User reported settings copy.h cannot be used; located file under archive/legacy-versions/esp32-display, renamed settings_copy.h without content changes. Archive README explains rename. Recursive source filename check passes; git diff whitespace check passes. No firmware rebuild/upload; user IDE confirmation pending.

## 2026-10-03 — Match main sketch to GitHub ZIP folder
- Renamed public root sketch rfid_spotify.ino → rfid_spotify-main.ino at user request; README explains ZIP/clone folder naming and preserving sibling sources, OTA application filename updated. Persistent checkout moved to github/rfid_spotify-main; original hardware sketch unchanged. Verified byte-identical rename; no runtime changes or device upload.
