# ESP32 Spotify RFID player

An MFRC522 reader selects Spotify albums, playlists, tracks or an artist's shuffled album catalog on a Spotify Connect speaker. A shared-SPI ILI9341 display shows album art. The Echo must already be available to Spotify Connect; this firmware does not invoke Alexa to wake an unavailable device.

## Provision and build

The existing local credentials have been preserved in **secrets.h**, which is excluded from Git. For a fresh checkout, copy **secrets.example.h** to **secrets.h** and supply Wi-Fi credentials, Spotify app credentials and the speaker name. A refresh token in that file only seeds empty NVS storage. Thereafter NVS is authoritative, including tokens renewed through the dashboard or rotated by Spotify. New firmware stores the token and PKCE/legacy grant type together in `auth_v2`, migrating from `ref_token` on first boot. Rolling back to old firmware will not read newly authorized PKCE credentials.

The connected device was identified by esptool as an **original ESP32**, not ESP32-C6. Select **ESP32 Dev Module** for that device and **Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)**. The 4 MB layout still assumes at least 4 MB physical flash; the earlier tentative C6-N4 identification does not establish this device’s capacity. Core **3.3.11** is used for the builds. The separate C6 profile remains available for an actual C6 device. Exact tested libraries are listed in **sketch.yaml**. The old NDEF_MFRC522 library is no longer used: **SafeNdef.h** and **RfidReader.h** provide a bounded, allocation-free read path inside this project. No machine-wide libraries need patching.

With the dependencies already installed:

```sh
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=min_spiffs --warnings all .
```

For a fresh toolchain, Arduino CLI's `--profile esp32-review` uses the versions in **sketch.yaml** (and may download them). The default application partition is too small. The tested larger scheme has two 1,966,080-byte application slots and a 128 KiB filesystem; NVS keeps the same address/size. Back up the existing device before migrating its partition table. Build success does not verify on-device heap use.

For a player without a TFT, compile the same source with display support disabled:

```sh
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=min_spiffs --build-property 'compiler.cpp.extra_flags=-DPLAYER_HAS_DISPLAY=0' .
```

This removes TFT/JPEG code and artwork downloads. The web dashboard, RFID reader, Spotify control and token-update interface remain available; display-only controls are hidden. The HTTPS Spotify reconnect flow works through the web UI in both builds; no screen or physical button is required. Provision its certificate before building, as described in [DEVICE_REAUTH.md](DEVICE_REAUTH.md). The experiment folder contains historical footprint measurements, not the production login implementation.

Pins are unchanged: SPI SCK 18 / MISO 19 / MOSI 23, RFID CS 5 / reset 4, TFT CS 15 / DC 2 / reset 22. Both chip selects are deasserted before SPI initialization.

## Administration

The internal-LAN build opens **http://spotify-player.local/** without a username/password. HTTPS reconnect also has no device login prompt; Spotify still requires its own sign-in/consent. CSRF/Origin checks and browser-bound OAuth state remain enabled. Set `PLAYER_REQUIRE_WEB_AUTH=1` in **DeviceConfig.h** to restore optional device authentication; that mode prints the saved admin password over USB Serial at **115200 baud**.

The dashboard shows uptime and the named last-reset reason. Certificate replacement restarts the HTTPS server only, now explicitly identified in logs. A second HTTPS availability message without another boot banner is not a device reboot. The old unauthenticated Telnet service remains removed. Outbound Spotify/CDN connections continue to verify TLS certificates.

Commands return **202 + job ID** immediately. The dashboard and renewal helper poll for that job's actual result. A queued command is not a claim of playback success. Codes include Spotify's 400/401/403/404/429, 409 for superseded/incompatible state, 502 for invalid upstream data, and 507 if a token or speaker was accepted in RAM but flash persistence failed. A 507 token write is retried by the worker; avoid rebooting until saved. A full queue returns 503. The dashboard keeps the latest eight job outcomes.

If a token update fails validation, the previous credential remains intact. A successful token update does not wait for speaker discovery. To change speakers, use **Find speakers**, choose a speaker, then **Select speaker**. The live device list is validated and transfer must succeed before the target changes. Volume controls read actual playback state rather than assuming a 50% starting volume.

## RFID behavior

Supported cards are MIFARE Classic 1K with NDEF keys, and NFC Forum Type 2 Ultralight/NTAG cards with an accessible capability container. Data capacity is bounded to 1008 bytes, with NDEF messages limited to 768 bytes and 16 records. Longer/unsupported tags are rejected with a diagnostic rather than overflowing a buffer.

Both UTF-8 NDEF Text records and URI records (uncompressed or `https://` prefix) can contain:

- `spotify:album:<22-character ID>`
- `spotify:playlist:<22-character ID>`
- `spotify:track:<22-character ID>`
- `spotify:artist:<22-character ID>`
- Equivalent `https://open.spotify.com/<type>/<ID>` links, optionally with a query string.

UTF-16, chunked NDEF records and localized/unsupported URL forms are rejected. Artist cards preserve the prior special handling for the two configured artist IDs and choose from album/single offsets without consuming an entry until playback succeeds. This preserves the original catalog-order assumption for those special cases.

A card held on the reader is processed once. Remove it for at least about 1.5 seconds before presenting it again, including after a failed read. Brief RF timeouts do not release the held-card latch; reader faults and resets preserve it. Recovery explicitly drives the reset pin and disables the library’s conflicting reset-pin management. Failed recovery logs now include the reader register values. The reader task polls independently of HTTP, TLS, retries and the dashboard; SPI and JPEG operations stay in that one task. Artwork decoding polls RFID between MCU blocks. Reader communication/configuration checks run every 30 seconds, with more frequent recovery when offline. An empty RF field alone does not trigger resets.

Spotify jobs have one worker and bounded I/O timeouts. Retriable playback failures get at most two scheduled retries; Retry-After is respected. A newer scan or speaker selection supersedes a pending playback retry. Artwork is optional, capped at 64 KiB, fully transferred and decoded before replacing the cached image. Low-memory conditions skip artwork.

## Device-hosted Spotify reconnect

Use **Reconnect Spotify** on the dashboard, or open **https://spotify-player.local/** directly. The HTTPS page uses the same admin credentials. After Spotify login, wait for **Spotify reconnected and saved**, then scan a card. See [DEVICE_REAUTH.md](DEVICE_REAUTH.md) for certificate trust, redirect registration and device acceptance testing. Certificates renew automatically on-device, 90 days before expiry, including recovery after extended power-off. The dashboard reports renewal state, remaining certificate/authority lifetime and token persistence failures. Trust setup is required once per browser/device and again when the authority expires in about ten years.

Use **Test saved refresh token** in **Spotify connection** after reconnecting or replacing a token. It forces a real refresh request even if the access token is still valid and displays the matching queued job’s result. It distinguishes rejected credentials, rate limiting, network/time problems and a failed flash save. It does not trigger playback. The existing **Validate and save** form tests a pasted replacement token before installing it.

## Token helpers (optional fallback)

Install **requirements.txt** into your Python environment. Set **SPOTIFY_CLIENT_ID** and **SPOTIFY_CLIENT_SECRET** there; the scripts contain no embedded credentials. Register **http://127.0.0.1:8080/callback** for the same Spotify app.

```sh
python3 renew_token.py --url http://spotify-player.local
```

The script prompts for the device's admin password, or uses **ESP32_ADMIN_PASSWORD** if provided. It starts the callback server before opening the browser, validates a one-use OAuth state, uses network timeouts, submits the token and waits for a confirmed job result. If installation is unconfirmed, it offers to save the newly issued token in a new owner-only file.

To obtain a token without sending it to the ESP32:

```sh
python3 getrefreshtoken.py --output spotify-refresh.token
```

The output path must not exist. Tokens are not printed. Keep credential files private and rotate any previously shared client secrets or token logs.

## Verification

```sh
python3 tests/run_tests.py
```

This uses Clang ASan/UBSan and the local ArduinoJson sources (override with `--arduino-json /path/to/ArduinoJson/src`). It tests actual production parser, RFID read, album-order and Spotify-client code with mocked hardware/transport, plus the Python OAuth/update workflow. It includes 50,000 deterministic malformed NDEF cases, all supported Type-2 capacity boundaries, Classic block mapping, token failure/rotation/persistence, speaker selection, 401 retry, 429 cooldown, unknown-length bodies and truncated downloads.

Hardware testing remains necessary. Run a 72-hour soak with timestamps and monitor scan counts, failed reads, recovery count, polling gaps, minimum/largest heap, task stack headroom and reset reasons. Include Wi-Fi outages, Echo disappearance, token renewal followed by reboot, removal mid-read, held cards, rapid card changes and slow dashboard clients. A good version register proves SPI communication, not that every RF read will succeed.

For the real certificate-generation test, build Mbed TLS 3.6.6 locally with CMake (programs/tests disabled), then run:

```sh
python3 tests/run_identity_tests.py --mbedtls /path/to/mbedtls-3.6.6
```

This executes `DeviceIdentity.cpp` against real Mbed TLS with mocked NVS, tests failed writes, restored certificates, expired/future dates, and verifies the generated chain independently with OpenSSL. It requires the locally provisioned automatic identity and its matching `https-auto-private` CA files.
