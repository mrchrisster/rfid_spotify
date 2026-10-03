# HTTPS / device-hosted reconnect flash experiment

**Historical experiment:** Production HTTPS reconnect is now implemented in `DeviceAuth.cpp`; see [device setup](../../DEVICE_REAUTH.md). These measurements predate that implementation, and rerunning the probe on current sources would include both HTTPS implementations.

The original goal was to measure whether the current RFID/Spotify firmware can also host an HTTPS callback and PKCE browser flow, without requiring a TFT. This experiment itself does **not** implement a deployable Spotify login flow.

Run from the project root:

```sh
python3 experiments/https_flash/measure.py
```

The script copies the current firmware to an owner-only temporary directory, builds screen and screenless baselines, adds the real ESP-IDF HTTPS server plus a temporary ECDSA certificate/key, and builds the probe. It also tests the standard `min_spiffs` application partition on the generic 4-MB ESP32 target. It never flashes or contacts Spotify, and it never edits the production partition selection. Override `--cli` if Arduino CLI is installed elsewhere.

The linked probe includes three real web handlers: a browser landing page, a Reconnect button that exercises PKCE verifier/challenge/state generation, and callback query/state parsing. The callback intentionally returns 501 instead of exchanging an authorization code. No certificate/private key is added to the repository. The generated certificate is only a storage/linking fixture, not a trusted device certificate.

`measurement.json` contains compiler-reported flash/static-RAM figures, build outcome, and locations of the full logs and isolated build artifacts. A nonzero exit with a size over the partition limit is an expected result for an oversized variant.

**Screenless requirement:** reauthentication must start and complete from the authenticated web UI. The future TFT reminder and QR code must be optional; no step can depend on viewing or pressing anything on a display. Each physical device will need a distinct stable local hostname and registered HTTPS callback, with a certificate trusted by the browser. The existing HTTP admin/token interface already works in the new `PLAYER_HAS_DISPLAY=0` build.

**What size results do not prove:** this is not the complete OAuth feature. Enrollment/trust provisioning, PKCE authorization-code exchange integration, certificate lifecycle, protected session handling, expiry reminders and the final UX still need implementation and testing. HTTPS session heap/stack usage must be measured on hardware alongside outbound Spotify TLS, RFID polling and artwork. Static-RAM totals do not include those dynamic allocations.

The larger partition is a build-only option. Confirm physical flash capacity and stored filesystem data before any partition migration. In the tested core, `min_spiffs` reserves two 1,966,080-byte application slots and reduces the filesystem to 128 KiB, retaining the NVS offset and size. No partition was written to a device.

## Historical generic ESP32 results — 2026-09-19

| Build | Application bytes | Partition bytes | Free / over limit | Result |
| --- | ---: | ---: | ---: | --- |
| Current firmware, TFT | 1,283,552 | 1,310,720 | 27,168 free | Fits |
| Current firmware, screenless | 1,224,156 | 1,310,720 | 86,564 free | Fits |
| HTTPS/PKCE probe, TFT | 1,324,124 | 1,310,720 | 13,404 over | Too large |
| HTTPS/PKCE probe, screenless | 1,264,900 | 1,310,720 | 45,820 free | Fits |
| HTTPS/PKCE probe, TFT, larger app partition | 1,324,156 | 1,966,080 | 641,924 free | Fits |

The probe adds 40,572 bytes to the TFT build and 40,744 bytes to the screenless build. The TFT probe fits the standard `min_spiffs` partition with 641,924 bytes spare; the screenless probe fits the default partition with 45,820 bytes spare. The small binary-size difference between partition builds comes from the build configuration.

Recommendation: use the larger application partition for a screen-equipped 4-MB board after confirming its actual flash capacity/layout. The screenless variant has enough room for this probe in the current partition, but final OAuth and certificate provisioning still need their own build/runtime verification. Both baseline variants compile successfully; the screenless builds have no compiler warnings. Dashboard JavaScript passes `node --check`.

## Historical ESP32-C6 results (before production HTTPS integration)

`measurement-esp32c6.json` records the corrected target. Baseline TFT: 1,402,502 bytes; baseline screenless: 1,336,202 bytes. Both exceeded the 1,310,720-byte default slot. The TFT HTTPS probe fit `min_spiffs` at 1,451,746 bytes, leaving 514,334 bytes. These replace the generic-target partition recommendation for this hardware; see the production results in `review/REAUTH.md`.
