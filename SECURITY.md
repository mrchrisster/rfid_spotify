# Private provisioning material

Keep Spotify credentials, Wi-Fi passwords, private certificate/key material and personalized firmware out of Git. Use `secrets.example.h` to create ignored `secrets.h`; generate an ignored `DeviceCertificate.h` locally. Public firmware release artifacts must never contain provisioned secrets.

## Historical exposure

Earlier revisions contained generated HTTPS private keys and provisioning values. The reorganized tree omits private material and sanitizes archived configuration, but **Git history still retains previously committed files**. Treat previously published private keys and any real credentials as exposed: regenerate device/issuer/CA identities, revoke or rotate affected Spotify credentials and tokens, and update devices/trust settings in a planned migration. Removing files does not revoke keys.

This restructuring does not rewrite history or rotate the running devices. Coordinate those separately to avoid unexpectedly breaking reconnect or playback.
