# Automatic certificate renewal — 2026-09-20

Implemented in `DeviceIdentity.cpp`, `CertificatePolicy.h`, `DeviceAuth.cpp`, provisioning and dashboard code. Setup: [DEVICE_REAUTH.md](../DEVICE_REAUTH.md).

The ESP32 signs a 397-day server certificate on its first time-synchronized boot and subsequently renews 90 days before expiry. It recovers after extended power-off or a corrected future clock. Replacement is validated against the pinned issuer and hostname, checked against the server key, atomically persisted to a separate NVS namespace, and read back before activation. HTTPS stops before its borrowed PEM buffer changes; a stop failure retains the running server and retries. RFID hardware ownership/pins are unchanged.

The root signing key stays off-device. A device-specific delegated key is embedded, with DNS name constraints, excluded IP ranges, and no subordinate-CA authority. The delegated certificate and root expire after approximately ten years. Browser trust is installed once for this authority; new devices/browsers or authority replacement require trust setup again. This removes annual certificate uploads, not all possible future maintenance.

## Final builds

Arduino ESP32 core 3.3.11, `esp32:esp32:esp32c6:PartitionScheme=min_spiffs`, assumed 4 MB physical flash:

| Variant | Application bytes | App slot bytes | Free bytes | Static RAM bytes |
| --- | ---: | ---: | ---: | ---: |
| TFT | 1,484,378 | 1,966,080 | 481,702 | 56,500 |
| Screenless | 1,418,018 | 1,966,080 | 548,062 | 52,372 |

Both pass. Logs remain `/private/tmp/rfid-https-c6-build.log` and `/private/tmp/rfid-https-c6-headless.log`. Compile size does not establish peak TLS/crypto heap usage on hardware.

## Verification

- Full native ASan/UBSan regressions pass, plus nine Python tests and dashboard JavaScript syntax checking.
- Certificate policy tests cover renewal thresholds, expired certificates, unsynchronized time, UTC conversion across leap years/2038, issuer-lifetime capping, failed writes and readback mismatch retaining active data.
- HTTPS handler tests cover deferred reload after a simulated server-stop failure: no certificate-buffer activation until stop succeeds.
- Provisioning tests verify private-file permissions, CA-preserving provisioning renewal, named-curve encoding and exclusion of the root private key from the firmware header. A validly signed certificate for an unrelated DNS hostname is rejected by OpenSSL's name-constraint verification.
- Downloaded/built upstream Mbed TLS **3.6.6**, matching the pinned ESP32 core's version, into `/private/tmp/rfid-mbedtls-host`. Ran `tests/run_identity_tests.py` against actual `DeviceIdentity.cpp` with real crypto and mocked NVS. It signs/validates a leaf, parses the complete chain, restores it after a simulated reboot without writing again, replaces expired/future-clock certificates, retains the working chain on persistence failure, and refuses issuance near issuer expiry. Independent OpenSSL verification of the firmware-generated chain passes.
- The real-crypto test caught and fixed macOS OpenSSL's explicit-curve default: generated keys now explicitly use named P-256 curves accepted by Mbed TLS. The earlier manual certificate fixture also used that incompatible encoding and must not be used for deployment.

## Provisioning and remaining hardware checks

Use the **new `https-auto-private/ca.crt`** for trust. The older manual files and the initial incompatible automatic fixture were retained in ignored private backup directories. No browser trust store was modified, no firmware flashed, and no live Spotify authorization completed.

Name constraints are noncritical to let this mbedTLS implementation load its own chain; validators that support name constraints enforce them (tested with OpenSSL). Actual browser trust still needs verification on the intended phone/computer. A read-only macOS `security verify-cert` check reported certificate verification with no error but returned nonzero alongside unverified Certificate Transparency, so it is not counted as a successful browser acceptance test.

On-device first-boot signing, NVS behavior during actual power interruption, simultaneous TLS/artwork heap usage, successful phone/browser login, and the 72-hour reliability soak remain necessary. The first synchronized boot logs `[HTTPS] Certificate renewed and saved`; a reboot should then load that certificate without signing/writing it again. Spotify reauthorization still requires human approval when Spotify invalidates the refresh token.
