# Device-hosted Spotify reauthentication

The ESP32 now serves a real HTTPS reconnect page. A phone/computer browser signs into Spotify, then returns the authorization code to the device. The device exchanges it with PKCE, saves the refresh token and grant type together in NVS, and refreshes access tokens automatically. No Python token helper or external callback server is involved in this flow. Both TFT and screenless builds use the same web UI.

## First-device setup

A private certificate and key have been generated locally for `spotify-player.local` in `DeviceCertificate.h` (ignored by Git). The public CA certificate to install is **https-auto-private/ca.crt**. The root CA key stays on your computer. Firmware contains a delegated signing key restricted to this device’s DNS namespace, so it can renew its own server certificates. This replaces the earlier manual-certificate setup: if you installed its old CA, install the new **https-auto-private/ca.crt** once. Old provisioning files have been preserved separately. Do not distribute `ca.key`, `device.key`, the generated header or firmware binaries containing your credentials.

1. Install/trust `https-auto-private/ca.crt` on the phone/computer used for login. On macOS, import it into Keychain Access and explicitly trust it for SSL. On iPhone/iPad, installing a certificate profile alone does not enable SSL trust: also enable full trust under Settings → General → About → Certificate Trust Settings ([Apple instructions](https://support.apple.com/en-us/102390)). Trust only your own generated CA. Do not use a browser certificate-warning bypass as the deployment method.
2. In the Spotify developer dashboard for the **same client ID configured in this firmware**, register exactly `https://spotify-player.local/callback`. Spotify requires HTTPS for non-loopback callbacks and an exact registered URL match ([Spotify requirements](https://developer.spotify.com/documentation/web-api/concepts/redirect_uri)). Your browser must be able to resolve that mDNS name on the device's LAN. Use the browser throughout the flow; switching browsers or handing the callback to another app loses the session cookie.
3. For the device identified by esptool as ESP32, select ESP32 Dev Module and build the firmware with `PartitionScheme=min_spiffs` (commands below), then upload to the first test device with its existing working board/USB settings. No wiring changes are required. Back up the existing flash/configuration before changing the partition table; do not select “erase all flash.” No upload has been performed by this implementation task.
4. Open the existing HTTP dashboard and choose **Reconnect Spotify**, or directly open `https://spotify-player.local/`. The certificate must validate without a warning. Choose **Continue to Spotify**; no device username/password is required in this internal-LAN build. Complete Spotify login and wait for **Spotify reconnected and saved**.
5. Scan a known card, reboot and scan again. Then use **Test saved refresh token** in the dashboard’s Spotify connection section and verify another scan. This validates persistence and subsequent PKCE refresh, independently of hourly automatic renewal.

For a new checkout, provision the certificate before building:

```sh
python3 tools/provision_https.py --hostname spotify-player
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=min_spiffs --warnings all .
```

Screenless:

```sh
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=min_spiffs --build-property 'compiler.cpp.extra_flags=-DPLAYER_HAS_DISPLAY=0' --warnings all .
```

Without `DeviceCertificate.h`, firmware still builds and plays, but HTTPS reconnect is disabled. The dashboard explicitly reports it unavailable. There is no bundled shared private key.

## Second device

Give the second player a different hostname, e.g. `spotify-player-2`, and its own leaf certificate/key. Generate it in a separate private directory and build with that generated header. Register `https://spotify-player-2.local/callback` and trust its CA on the browser. The hostname in the header controls both mDNS and the OAuth callback; changing one manually without the certificate and Spotify registration will fail. Keep each device's provisioning files so later builds use the right identity.

## Automatic certificate renewal

The device issues a **397-day** server certificate on its first synchronized boot, validates the signature, hostname and matching private key, then saves it in the separate `tls_identity` NVS namespace. This exercises the real renewal path immediately, instead of waiting a year to discover a provisioning problem.

It checks hourly and renews **90 days before expiry**. If it was powered off past expiry, it renews after reconnecting and obtaining valid time. A certificate issued under an incorrect future clock is also replaced once the clock is corrected. Routine Spotify reauthentication and certificate renewal need no computer or external signing server.

The certificate is committed as one atomic NVS value and read back before replacing the active certificate. Failed writes keep the current HTTPS certificate and retry hourly. HTTPS is briefly restarted only after the certificate is ready; its borrowed PEM buffer stays unchanged until the old server has stopped. RFID runs independently. Certificates and Spotify credentials use separate storage namespaces. The dashboard reports renewal status, certificate days remaining and signing-authority days remaining.

**One-time trust lasts about ten years**, not forever. The root and delegated signing authority expire in approximately ten years; the dashboard warns near the end, and renewed server certificates are capped below the signing authority’s expiry. Reprovisioning then requires installing the new root certificate. A new phone/browser also needs the existing public CA installed. Spotify/browser policy changes may still require future firmware maintenance.

The unrestricted `ca.key` stays off-device. The on-device issuer is constrained to `spotify-player.local` and its DNS subtree, excludes IP addresses, and cannot issue subordinate CAs. The constraint is encoded as noncritical because this ESP-IDF mbedTLS build cannot load a server chain with critical name constraints; supporting browser validators still enforce it. OpenSSL tests reject an unrelated hostname signed by this delegated key. Actual target-browser trust remains part of the hardware acceptance test.

For a second device, use a separate directory and hostname; `--replace-header` explicitly selects a newly provisioned identity for the next build. Keep each device’s provisioning files private. The generated header and firmware contain the device’s signing/server keys, but never the unrestricted root key.

The optional provisioning `--renew` command still preserves an existing automatic CA/issuer when preparing a new build; it is **not required for routine renewal**. Legacy headers without a delegated issuer continue in manual mode, which the dashboard identifies explicitly.

## Behavior and failure recovery

Device login is disabled by default for this internal-LAN installation. `PLAYER_REQUIRE_WEB_AUTH=1` restores optional admin authentication over TLS. Login POSTs require the correct Origin and a browser-bound CSRF token. The form uses `Referrer-Policy: strict-origin` so the browser retains the POST Origin while keeping callback codes out of referrers. Callback state is random, one-use and expires after five minutes; a Secure/HttpOnly/SameSite=Lax cookie binds the callback to the browser that started login. Replay, wrong-state, duplicate-state and wrong-browser callbacks are rejected. Only one reconnect is active at a time; opening the start page again replaces an unfinished session.

The callback queues code exchange to the existing Spotify worker, which remains the sole owner of Spotify state and post-boot Preferences writes. A denied/failed login retains the existing connection. A successful login is reported only after saving. A flash-write failure retains the newly issued credentials in RAM, reports **507**, and retries persistence through normal maintenance. Do not reboot while the dashboard reports an unsaved token. The result page can be refreshed after the retry succeeds.

If Wi-Fi, NTP time, rate limiting or the token endpoint prevents code exchange, restart reconnect once the underlying problem has cleared. The device does not retry a consumed authorization code automatically. Certificate renewal avoids starting while an authorization-code exchange is queued/running. TLS certificate checks on outbound Spotify connections remain enabled. No authorization codes, verifier values or tokens are added to application logs.

The HTTPS server runs below RFID task priority, with one inbound TLS socket and bounded request sizes/timeouts. HTTP administration remains available. Compile size and native tests cannot establish peak TLS heap use, hardware scheduling behavior or real-browser interoperability.

## Hardware acceptance test

- On first boot, wait for `[HTTPS] Certificate renewed and saved`. Confirm the dashboard shows automatic renewal and about 397 days remaining, then reboot and verify it loads the saved certificate without signing/writing again.
- Confirm reconnect from the browser on both display and screenless devices, without relying on the display.
- Cancel Spotify consent: the prior account must still play. Try an expired login (>5 minutes), browser Back/reload after completion, and a second browser; no replay should install tokens.
- After successful login, reboot, use **Test saved refresh token** to force an access-token refresh, select the Echo if needed and scan cards. Also allow a full automatic refresh interval to pass.
- Disconnect/reconnect Wi-Fi before and during login. Retry from the start page. Verify hostname resolution and HTTPS recover without rebooting.
- While repeatedly opening HTTPS and fetching artwork, monitor minimum/largest free heap, RFID maximum polling gap, failed reads and reset reasons. Close the reconnect page after completion.
- Run the existing 72-hour reliability soak. A successful compile and mocked callback test are not a substitute for this.

## Troubleshooting the reconnect form

A crossed-out HTTPS indicator means browser certificate validation has not succeeded. Install and trust the current `https-auto-private/ca.crt`, use the exact `.local` hostname, and close old reconnect tabs. Clicking through a certificate warning is not the trust setup. The form now separately reports missing browser cookies, expired/reused sessions and mismatched origins. Earlier firmware sent `Referrer-Policy: no-referrer`, which can turn a native form POST's Origin into `null`; that header conflict is fixed in the current source without disabling the origin/CSRF checks.
