# Unattended firmware updates — proposal, not implemented

## Objective / delivery
- Device pulls releases; no laptop/controller running continuously, no per-update click. Human publishes tested releases; automated build/release pipeline can produce signed assets later.
- Host small signed manifest + two credential-free app binaries (ESP32 TFT/headless) on static HTTPS hosting; e.g. release assets/CDN. No provider provisioned. Never publish current personalized firmware without checking/removing compiled secrets; keep provisioned Wi-Fi/Spotify/identity in NVS and verify migration first.
- Manifest binds release/version, hardware chip, display variant, partition-layout/schema compatibility, byte length, SHA256, HTTPS artifact URL, channel, minimum updater version. Embedded public verification key, signing private key only release environment. Authenticate manifest and hash binary before committing boot partition; bound manifest size/URL redirects/total bytes. Reject downgrade/repeated failed version; provide signed higher-version recovery release. Key rotation needs supported overlap, not expiring device trust accidentally.

## Device state machine
1. Once/day with jitter, check manifest; connectivity/time failure→bounded backoff; no flash writes for unchanged versions/check counters.
2. Validate signed metadata and exact local variant/chip/slot capacity. Preserve display/headless choice; no remote credentials in URL/logs.
3. Schedule install only when confidently idle (e.g.5min no playback/card/job), optional overnight window. Unknown speaker state is not idle. Never interrupt active audiobook. Expose disable/channel/manual check/status in web UI.
4. Reuse `FirmwareUpdateState` prepare acknowledgments: capture supported bookmarks, persist tokens/dirty data, release artwork, stop reconnect HTTPS to avoid simultaneous TLS pressure. Auto updater and manual uploader mutually exclusive; single owner of update stream/TLS.
5. Stream small chunks via verified HTTPS directly to inactive slot; incremental SHA256; enforce length/ESP32 image header/partition limit. Do not allocate full image, use temporary SPIFFS, update bootloader or partition table remotely. Abort network/verification/write failure without selecting new slot; release workers/retry later.
6. Verify signed digest/image before finalizing/selecting boot slot; bounded reboot delay after successful install. Token/network identity/bookmarks persist; storage migrations must remain readable by old firmware during trial.
7. Trial boot: defer Arduino automatic acceptance (`verifyRollbackLater` in pinned3.3.11); watchdog-bound local health window validates loop/worker progress, storage access and initialization without writes destroying rollback compatibility. RFID fault versus preexisting hardware failure needs explicit policy; no requirement for card scan, display hardware on headless or external Spotify/router reachability to accept firmware. Mark valid after local checks; crash/watchdog/failed validation rolls back. Persist failed release once to prevent reinstall loop.

## Evidence / prerequisites
- Current manual `FirmwareUpdate.h`: inactive app streaming, size/header checks, quiescence, timeout; no signature/manifest/automatic poll/trial-health policy.
- Current min_spiffs OTA slots1966080B each, TFT1568276B/headless1440712B; new updater size remains to measure.
- `/private/tmp/player-wifi-tft/sdkconfig`: CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y. Installed Arduino3.3.11 `cores/esp32/esp32-hal-misc.c`: verifyOta defaults true; verifyRollbackLater hook exists. Actual device bootloader must support trial states; if older/incompatible, one-time USB bootloader setup may be needed. Do not claim it is required yet.
- Reference: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/ota.html .
- Initial updater installation once via existing web OTA where bootloader compatible. Future updates unattended; hosting availability needed only for new releases, ordinary playback unaffected.

## Validation before enabling
- Wrong chip/variant/size/signature/hash/version, truncated manifest/download, failed storage, timeout, concurrent manual OTA, playing/unknown speaker all block safely.
- Power loss during download/finalization, crash/hang during trial, preserved settings/credentials and rollback-compatible data tested on spare physical device, TFT and headless.
- Ensure failed version backoff, main UI responsiveness, download TLS heap headroom, retained old slot and non-disruptive idle detection. Roll out to one test device before stable release.
