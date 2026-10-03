# Production device-hosted reconnect verification — 2026-09-19

**Historical snapshot:** automatic certificate renewal and newer build sizes are recorded in [CERTIFICATE_RENEWAL.md](CERTIFICATE_RENEWAL.md). Its certificate-lifecycle findings supersede the manual-renewal limitation below.

Implemented in `DeviceAuth.cpp`, `OAuthSession.h`, `SpotifyClient.cpp`, the main sketch and `Dashboard.h`. Setup and hardware acceptance procedure: [DEVICE_REAUTH.md](../DEVICE_REAUTH.md).

## Build results

ESP32 Arduino core 3.3.11; target `esp32:esp32:esp32c6:PartitionScheme=min_spiffs`, 4 MB flash. Both builds include the generated local ECDSA HTTPS certificate and complete reconnect flow, not the earlier footprint probe.

| Variant | Application bytes | App slot bytes | Free bytes | Static RAM bytes |
| --- | ---: | ---: | ---: | ---: |
| TFT | 1,465,190 | 1,966,080 | 500,890 | 52,340 |
| Screenless | 1,398,838 | 1,966,080 | 567,242 | 48,212 |

Both compile successfully. The screenless build has no compiler warnings. TFT warnings remain confined to the existing third-party JPEGDecoder/picojpeg negative shifts and unused variables. No new production-source warnings. These sizes supersede the historical generic ESP32 and C6 probe measurements. Static RAM is not peak heap usage.

Local logs: `/private/tmp/rfid-https-c6-build.log` and `/private/tmp/rfid-https-c6-headless.log`. Build artifacts are in the matching private directories; firmware contains credentials and must not be shared publicly.

## Automated verification

`python3 tests/run_tests.py` passes:

- Existing core, 50,000 malformed NDEF, RFID boundaries/mapping and Spotify transport/state tests under ASan/UBSan.
- Callback decoding rejects truncation, controls, malformed percent escapes and duplicate state keys, including encoded aliases. Session tests cover wrong browser/state, expiry across millisecond rollover, and one-use consumption.
- Actual production HTTPS handlers execute against a mocked ESP HTTP/queue platform. Tests cover admin authentication, cookie flags, Origin/CSRF rejection, missing-cookie and duplicate/wrong-state callbacks, accepted callback queuing, one-use rejection, Spotify code exchange/persistence, result-page authorization, expiry and cancelled consent retaining the prior connection.
- Actual Spotify client tests cover PKCE exchange request encoding and no Basic/client-secret authentication, persisted grant type across a simulated reboot, refresh responses without replacement refresh tokens, malformed/denied authorization retaining old state, rejected scopes, failed token persistence followed by retry, rotation and legacy-helper compatibility.
- Eight Python tests pass, including real OpenSSL generation/verification, private header permissions, refusal to overwrite accidentally, CA-preserving renewal and invalid hostname rejection. Native HTTPS test SHA uses a macOS CommonCrypto adapter; hardware RNG/network/TLS remain mocked.
- Dashboard JavaScript passes `node --check`.

## Deliberate limits

No firmware was flashed, no device was rebooted, no Spotify account was authorized, and no browser trust store was modified. On-device TLS peak memory, browser/mDNS/Spotify interoperability and the 72-hour reliability soak remain unverified. One-time browser CA trust and Spotify callback registration are required. Leaf certificate renewal is manual at 397 days (the dashboard reports time remaining); this release does not implement automatic certificate renewal or web certificate upload. The CA private key stays off-device.
