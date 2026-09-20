# ESP32 Spotify RFID Player

Play music on an Echo or another Spotify Connect speaker by placing an RFID card on a reader. Cards can select albums, playlists, tracks, or an artist's album catalog. An optional ILI9341 screen displays album artwork, while a local web dashboard provides playback controls and Spotify account setup.

```text
RFID card → ESP32 + MFRC522 → Spotify Web API → Spotify Connect speaker
                   ↕
             Web dashboard
```

The ESP32 controls playback; the speaker streams the audio. The speaker must already be available in Spotify Connect. The firmware cannot invoke Alexa to wake an unavailable Echo.

## Features

- Read Spotify links and URIs from supported NDEF cards.
- Play/pause, skip, adjust volume, and select a speaker from the web dashboard.
- Run with or without a TFT display, including account setup on screenless devices.
- Reconnect to Spotify through a device-hosted HTTPS page using OAuth with PKCE.
- Refresh access tokens automatically and persist replacement refresh tokens in flash.
- Renew the local HTTPS certificate automatically before it expires.
- Suppress repeated scans while a card stays on the reader.
- Recover reader communication, reconnect Wi-Fi, and retry transient playback failures.
- Inspect logs, reset reasons, memory statistics, reader health, and queued command results.

## Requirements

### Hardware

- An ESP32 development board with at least 4 MB flash for the documented partition layout.
- An MFRC522 RFID reader and supported NDEF cards.
- Optional ILI9341 TFT display.
- A stable power supply and Wi-Fi with internet access.
- An Echo or another speaker available to the same Spotify account through Spotify Connect.

The hardware-tested target is an **original ESP32**, using **ESP32 Dev Module** in Arduino IDE. A separate ESP32-C6 build profile is included, but C6 hardware and its pin mapping have not been validated. Select the board matching the actual chip.

### Software and account

- Arduino IDE or Arduino CLI.
- ESP32 Arduino core **3.3.11** and the libraries pinned in [sketch.yaml](sketch.yaml).
- Python 3 and OpenSSL for initial HTTPS provisioning.
- A Spotify developer app and a Spotify Premium account. Spotify requires Premium for [Web API playback control](https://developer.spotify.com/documentation/web-api/reference/start-a-users-playback).

## Wiring

The default pin assignments are for the original ESP32:

| Signal | ESP32 GPIO | Connection |
| --- | --- | --- |
| SPI SCK | 18 | Reader and optional TFT clock |
| SPI MISO | 19 | Reader and optional TFT MISO |
| SPI MOSI | 23 | Reader and optional TFT MOSI |
| RFID CS / SDA | 5 | MFRC522 chip select |
| RFID reset | 4 | MFRC522 reset |
| TFT CS | 15 | Optional display chip select |
| TFT DC | 2 | Optional display data/command |
| TFT reset | 22 | Optional display reset |

Use a common ground and the supply voltage required by each module. The reader and display share SPI, with separate chip-select signals. Screenless builds keep the same reader pin assignments.

## Setup

### 1. Configure the player

Open `esp32SpotifyAlexa_v5_tft_ndef.ino` in Arduino IDE, or run the following commands from the project directory.

```sh
cp secrets.example.h secrets.h
```

Edit `secrets.h` with your Wi-Fi credentials, Spotify app credentials, and target speaker name. The speaker name should match the name shown by Spotify.

Leave `refreshToken` empty to authorize through the web dashboard. It is an optional bootstrap credential: once a token is saved on the device, the saved token takes precedence over this field.

For a screenless player, change the default `PLAYER_HAS_DISPLAY` value to `0` in [DeviceConfig.h](DeviceConfig.h). Leave it at `1` for the TFT version.

### 2. Provision HTTPS

Generate the player's local HTTPS identity before compiling:

```sh
python3 tools/provision_https.py --hostname spotify-player
```

This creates:

- `DeviceCertificate.h`: private identity material included in the firmware.
- `https-auto-private/`: certificate authority, certificates, and private keys.

Trust **`https-auto-private/ca.crt`** on the computer or phone used for Spotify reconnect. Without this trust step, the browser will show a certificate warning. The certificate is issued by your local authority, not a publicly trusted authority.

Keep these generated files private. They are excluded by `.gitignore`, along with `secrets.h`. Do not publish firmware binaries containing your credentials or keys.

Detailed trust and renewal instructions are in [Device-hosted Spotify reauthentication](DEVICE_REAUTH.md).

### 3. Register the Spotify redirect

In the settings for the Spotify developer app matching your `clientId`, add this exact redirect URI:

```text
https://spotify-player.local/callback
```

The scheme, hostname, path, and trailing slash must match the firmware's redirect exactly. For a different provisioned hostname, change the registered URI accordingly.

### 4. Build and upload

In Arduino IDE, install the dependencies listed in [sketch.yaml](sketch.yaml), then select:

| Setting | Value |
| --- | --- |
| Board | ESP32 Dev Module |
| Flash size | Match the physical board; at least 4 MB for this layout |
| Partition scheme | Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS) |
| Erase All Flash | Disabled when preserving saved settings |
| Serial Monitor baud rate | 115200 |

The default application partition is too small. The documented layout provides a **1,966,080-byte application slot**. Although the partition scheme reserves OTA slots, the project does **not** implement firmware OTA updates. Back up an existing device before changing its partition layout.

Arduino CLI can use the pinned profile, downloading dependencies as needed:

```sh
arduino-cli compile --profile esp32-review .
```

For a screenless build without editing `DeviceConfig.h`:

```sh
arduino-cli compile --profile esp32-review \
  --build-property 'compiler.cpp.extra_flags=-DPLAYER_HAS_DISPLAY=0' .
```

Upload through Arduino IDE, or use the same CLI profile and your device's serial port:

```sh
arduino-cli upload --profile esp32-review --port /path/to/serial-port .
```

### 5. Connect Spotify

1. Open `http://spotify-player.local/`, or the IP address printed in Serial Monitor.
2. Click **Reconnect Spotify**. This opens the player's HTTPS hostname, even if you opened the dashboard by IP.
3. Sign in to Spotify and approve access.
4. Wait for **Spotify reconnected and saved**.
5. Return to the dashboard and click **Test saved refresh token**.
6. Use **Find speakers**, choose the target, and click **Select speaker** if needed.
7. Scan a programmed card.

To verify persistence, reboot the player and run **Test saved refresh token** again. A pass confirms that the restored refresh token can obtain a new access token.

## Preparing and using cards

Program cards with a separate NFC writing app or tool; this project reads cards but does not write them.

Supported content includes UTF-8 NDEF Text records and NDEF URI records containing:

```text
spotify:album:<22-character ID>
spotify:playlist:<22-character ID>
spotify:track:<22-character ID>
spotify:artist:<22-character ID>
https://open.spotify.com/<type>/<22-character ID>
```

Spotify URL query strings are accepted. Artist cards select from a shuffled album/single catalog.

Supported cards are MIFARE Classic 1K with NDEF keys and NFC Forum Type 2 Ultralight/NTAG cards with an accessible capability container. Reads are bounded to 1008 bytes of card data, 768 bytes of NDEF message data, and 16 records. UTF-16 text, chunked records, and unsupported URL formats are rejected.

A held card triggers once. Remove it for approximately **1.5 seconds** before presenting the same card again, including after a failed read. A different card can trigger immediately. Reader faults do not count as card removal.

## Screenless and multiple players

Screenless builds retain RFID, playback controls, diagnostics, and the full browser reconnect flow. They omit TFT/JPEG support and artwork downloads.

Use the same source code for multiple players, but provision a **unique hostname and HTTPS identity for each device**. Do not flash the same identity to two players on the same network.

For example, preserve the first player's generated header, then provision a second identity:

```sh
cp DeviceCertificate.h https-auto-private/DeviceCertificate.h
python3 tools/provision_https.py --hostname spotify-player-2 \
  --directory https-auto-private/player-2 \
  --header https-auto-private/player-2/DeviceCertificate.h
cp https-auto-private/player-2/DeviceCertificate.h DeviceCertificate.h
```

Register `https://spotify-player-2.local/callback` in the Spotify app, trust the second player's `ca.crt`, configure its intended speaker, and compile with the appropriate display setting and board target. Authorize the second player through its own dashboard.

Restore the first player's header before rebuilding for it:

```sh
cp https-auto-private/DeviceCertificate.h DeviceCertificate.h
```

## Authentication and long-term operation

Access tokens refresh automatically. When Spotify supplies a replacement refresh token, the firmware saves it together with its authorization mode. Browser reconnect also saves the credential to nonvolatile storage. Failed validation of a manually submitted replacement preserves the existing connection.

The log message **Token refreshed** means a new *access token* was obtained; it does not establish that the refresh-token value changed. **Browser reconnect saved** confirms that the browser authorization flow completed and its credential was saved.

The HTTPS leaf certificate lasts 397 days and renews on the device 90 days before expiry. Renewal also handles an expired leaf after a long power-off, once network time is available. Normal renewal does not require reflashing or trusting another CA. The provisioned authority lasts about ten years; replacing that authority eventually requires provisioning and browser trust again.

Revoked or rejected Spotify credentials still require browser reauthorization. Internet access, correct network time, Spotify API availability, and a reachable speaker remain dependencies. The firmware cannot guarantee years of operation without intervention.

### Local web access

Device login is **disabled by default** for use on a trusted internal LAN. Anyone with access to that LAN can operate the dashboard; do not expose it directly to the internet.

Set `PLAYER_REQUIRE_WEB_AUTH` to `1` in [DeviceConfig.h](DeviceConfig.h) to enable device authentication. That mode prints the stored admin credentials over USB Serial at 115200 baud. Browser Origin/CSRF checks and browser-bound OAuth state are enabled in both modes. Outbound Spotify and artwork connections verify TLS certificates.

### Optional computer-based token helpers

The normal reconnect flow runs on the player. Python helpers are also available as a fallback:

```sh
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install -r requirements.txt
```

Set `SPOTIFY_CLIENT_ID` and `SPOTIFY_CLIENT_SECRET` in the environment and register `http://127.0.0.1:8080/callback` in the same Spotify app.

```sh
python3 getrefreshtoken.py --output spotify-refresh.token
python3 renew_token.py --url http://spotify-player.local
```

The first command writes a token to a new private file without printing it. The second authorizes and submits a token to the player, then waits for the actual job result; it prompts for the device admin password or uses `ESP32_ADMIN_PASSWORD`. Keep token files private.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| `text section exceeds available space` | Select the documented Minimal SPIFFS partition scheme. |
| `This chip is ESP32, not ESP32-C6` | Select ESP32 Dev Module for an original ESP32. |
| `redirect_uri: Not matching configuration` | Register the exact HTTPS callback for this player's hostname in the app matching its client ID. |
| `ERR_CERT_AUTHORITY_INVALID` | Trust this player's generated `ca.crt` on the browser's device. |
| Dashboard works by IP but reconnect does not | The browser must resolve the provisioned `.local` hostname for HTTPS reconnect. |
| HTTPS handshake errors | Check certificate trust and hostname. A failed browser TLS connection alone does not mean an already completed token exchange failed. |
| Speaker missing or playback rejected | Make the speaker available through Spotify, then find and select it again. |
| Repeated RFID recovery failures | Inspect the logged reader registers, power, reset, and SPI connections. A single pre-initialization zero-register diagnostic is not itself a failure. |
| Same card does not retrigger | Remove it for at least about 1.5 seconds before presenting it again. |
| Black display | Confirm the boot banner reports `TFT=1`, then check display connections and power. |
| Error `507` after a credential update | The credential is usable in RAM but flash persistence failed. Avoid rebooting until the save warning clears. |
| Unexpected reboot | Capture the full Serial Monitor output before and after the boot banner, including reset reason and any panic, brownout, or watchdog messages. |

Dashboard commands are asynchronous: **202 plus a job ID means queued**, not completed. The dashboard polls for the result; successful Spotify operations commonly return 200 or 204. Only the latest eight job outcomes are retained.

## Code organization

| File | Responsibility |
| --- | --- |
| [Main sketch](esp32SpotifyAlexa_v5_tft_ndef.ino) | Startup, task coordination, hardware, and HTTP routes |
| [SpotifyClient.cpp](SpotifyClient.cpp) | Spotify API, access-token refresh, playback, and rate limiting |
| [Dashboard.h](Dashboard.h) | Browser dashboard and job polling |
| [SafeNdef.h](SafeNdef.h), [RfidReader.h](RfidReader.h) | Bounded NDEF parsing and card reads |
| [RfidPresence.h](RfidPresence.h), [RfidRecovery.h](RfidRecovery.h) | Held-card detection and hardware recovery |
| [DeviceAuth.cpp](DeviceAuth.cpp), [OAuthSession.h](OAuthSession.h) | HTTPS reconnect and PKCE sessions |
| [DeviceIdentity.cpp](DeviceIdentity.cpp), [CertificatePolicy.h](CertificatePolicy.h) | Certificate persistence and automatic renewal |
| [tools/provision_https.py](tools/provision_https.py) | Initial local HTTPS provisioning |

A hardware worker owns the shared SPI reader/display operations. A separate network worker processes Spotify jobs, so network requests do not directly block reader polling. Queues, response sizes, artwork buffers, and retries are bounded.

## Tests and reliability validation

Run the host regression suite:

```sh
python3 tests/run_tests.py --arduino-json /path/to/ArduinoJson/src
```

The suite uses Clang with AddressSanitizer/UndefinedBehaviorSanitizer, ArduinoJson 7.4.3 sources, Node.js, and the Python dependencies in `requirements.txt`. The current native HTTPS test harness uses macOS CommonCrypto.

Coverage includes malformed NDEF data, card capacity boundaries, held-card behavior, reader recovery, Spotify token rotation and persistence, API retries, rate limiting, HTTPS authorization handlers, certificate policy, and dashboard job results. Hardware and transport are mocked in these tests.

For certificate generation with real Mbed TLS, build Mbed TLS 3.6.6 locally with programs and tests disabled, provision the local identity, then run:

```sh
python3 tests/run_identity_tests.py --mbedtls /path/to/mbedtls-3.6.6
```

This exercises certificate generation and persistence with real cryptography and independent OpenSSL verification.

**Long-duration hardware reliability is still being validated.** Before unattended use, run at least a 72-hour soak covering held cards, repeated scans, Wi-Fi outages, speaker disappearance, and token refresh followed by reboot. Monitor reader recovery counts, polling gaps, free/minimum heap, task stack headroom, and reset reasons.

See [review/FIXES.md](review/FIXES.md) for the implementation history and [DEVICE_REAUTH.md](DEVICE_REAUTH.md) for detailed reconnect acceptance tests.
