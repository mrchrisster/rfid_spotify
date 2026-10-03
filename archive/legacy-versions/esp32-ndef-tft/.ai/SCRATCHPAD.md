# Session handoff

Updated: 2026-09-20. Maintain this file at every significant completion; replace stale active-task notes rather than accumulating a transcript.

## Active task / status
- **Observe the working firmware; longer soak pending.** User reports approximately two hours of very good operation and very fast card detection. No firmware changes requested.
- Latest update records user-reported device evidence only; no firmware edits or rebuild. Exact flashed build and quantitative latency/reset/heap measurements were not supplied.
- Latest feature: cover-duration web input changed from seconds to **minutes**. Existing seconds-based API/NVS retained; decimals and0=always-on supported.
- Immediately preceding feature: instant storybook loader + localized card/playback/cover errors. Implemented/tested/compiled; **not yet user-confirmed on hardware**.

## Next actions
1. On next user report, establish the flashed build/version before attributing behavior to current code.
2. Device-check loader: valid card → immediate animation → full-quality cover; held card does not restart; new card interrupts older result; malformed NDEF/read failure shows appropriate5s error then startup screen.
3. Device-check Settings → Display minutes: saved value persists/reloads correctly, decimals convert correctly,0 stays on. Confirm cover timeout → startup30min → blank.
4. Device-check early card before `[Auth] Token refreshed`: remains queued and plays after readiness. Startup auth improvement was reported, but no explicit successful pre-auth-card test was supplied.
5. Run repeated full-quality covers without reboot; then a ≥72h soak including Wi-Fi outage/recovery, auth refresh, held cards and speaker disappearance. Watch heap/stack headroom, RFID recovery counts and reset reasons.
6. When changing code, run relevant tests/builds and update all affected context files; distinguish mocked verification from device evidence.

## Validation ledger
| Area | Evidence / limits |
|---|---|
| Full-quality flash artwork | **User-confirmed working**.300px JPEG46,212B exceeded32,312B live-TLS budget; saved to flash and displayed by direct file decoding. Later log also showed57,796B JPEG succeeding with23,780B budget. |
| Startup auth latency | User reported improvement; logs showed refresh around5.8s, later3.0s after boot, vs prior~32s. Not a guaranteed timing bound. |
| OAuth reconnect/refresh | User previously confirmed reconnect saved and web refresh-token test passed. No guarantee of indefinite Spotify authorization. |
| Card presence | User confirmed held-card/removal behavior; latest report: approximately two hours working very well, cards detected very quickly. Qualitative device evidence, not a measured latency bound or completed long soak. |
| Loading/error screens | `LoadingState.h` host tests cover frame/error/timeout timing, stale results, generation reset and rollover; firmware compiled. Device visual/performance checks pending. |
| Dashboard redesign | Browser inspected phone390px and desktop1280px using **mock data**; Node tests cover navigation, saves, no on-load commands, headless mode and logs. |
| Minutes change | Node dashboard tests passed, including fractional conversion and legacy-duration handling. TFT compile passed. No native-code changes; full native suite was last run for loader. |
| Latest TFT build |1,519,492B /1,966,080B (77%); globals63,528B. After minutes change. |
| Last headless build |1,391,652B (70%); globals58,000B. After loader/errors, **before minutes-only HTML update**. |
| Main regression suite | Last full run passed for loader/errors; native ASan/UBSan + Python + JS. Separate `tests/run_identity_tests.py` not rerun in recent tasks. |

## Known issues / gotchas
- **RFID intermittent corruption**: historical all-zero/inconsistent registers, failed recovery loops, occasional startup delays. Recovery mitigations exist; root hardware/electrical cause not proven. Do not claim soak reliability solved.
- **Resets**: user previously reported an untouched reboot. Some later logs contain a second ROM boot but lack enough reset/backtrace context. DNS/SNTP assertion had a specific fix; do not attribute every reset to it or to power without evidence.
- **No tiny covers**: user explicitly forbids64px fallback. Width<240 is rejected in `Artwork.h`. Preserve this requirement.
- **Memory ownership**: flash-backed artwork has positive count with null image pointer; `Result.flashImage`/`DisplayJob.flashImage` carry ownership. Busy flag protects scratch file. Always release on drop/consume/failure.
- **No JPEG RAM copy after spooling**: this already failed in production. Use explicit file decoding. Large retained RAM caches can break subsequent TLS connections.
- **JPEGDecoder macro trap**: header aliases `SPIFFS`→`LittleFS`; retain sketch's `#undef SPIFFS` and explicit file-handle overload. Accidentally mixing filesystem objects breaks builds/reads.
- **SPIFFS logs**: initial mount failure can precede first-time formatting; do not diagnose corruption from that line alone. Nonempty mount failures must not trigger destructive formatting. Flash fallback incurs writes; long-duration wear/performance not characterized.
- **UI vs API units**: `Dashboard.h` uses `coverMinutes`; requests/status/storage use `cover_seconds`. Default10min=600s; startup30min is separate. Do not migrate saved values by treating seconds as minutes.
- **Animation/error policy**: display worker only; setup/debug screens retain priority. Loading timeout90s does not cancel queued play. New presentation ordering includes malformed cards. A successful204 playback job is not proof artwork rendered.
- **Board identity**: esptool identified ESP32, not C6. Do not switch presets or wiring because an older message guessed C6-N4.
- **Multiple devices**: shared `spotify-player.local` hostname can conflict on one LAN. Confirm identity/callback configuration when provisioning another device; do not blindly copy private identity.
- **Local HTTPS trust**: leaf auto-renewal does not make a private CA publicly trusted. Browser reconnect certificate trust and issuer expiry remain separate from Spotify token validity.
- **Working tree**: no `.git` directory observed. Secrets/private material exist locally and are ignored by `.gitignore`; do not print or include them in docs/artifacts.

## Useful commands / artifacts
- Workspace: `/Users/chrishelms/code/esp32SpotifyAlexa_v5_tft_ndef`.
- Test command: `python3 tests/run_tests.py`; build commands/versions in `.ai/ARCHITECTURE.md`.
- Recent temporary logs (ephemeral, not durable project records): `/private/tmp/minutes-tft.log`, `/private/tmp/loading-tests.log`, `/private/tmp/loading-tft.log`, `/private/tmp/loading-headless.log`.
- Historical decoded crash ELF: `/private/tmp/spotify-crash-fd184a391.elf` (may no longer exist); do not decode a new backtrace with a mismatched ELF.
- Historical original code arrived as chat attachments. Durable conclusions are ADR A10 and `.ai/CHANGELOG_AI.md`; do not rely on attachment paths surviving.

## Completion audit for this handoff
- [x] Architecture/ADRs reflect current code and user decisions.
- [x] Pending hardware checks and unresolved issues explicitly listed.
- [x] Notable history appended/backfilled with evidence boundaries.
- [x] Root `AGENTS.md` makes future sessions read/update these files.
- [x] No credentials copied; documentation paths checked. No firmware changes this task.
