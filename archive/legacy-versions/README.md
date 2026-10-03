# Legacy versions — archived, not maintained

The maintained ESP32 firmware is at the [repository root](../../README.md).
These snapshots preserve previous implementations for reference; fixes apply to the main project only.

| Folder | Historical implementation |
|---|---|
| `esp32/` | Original ESP32 player and card writer |
| `esp32-NDEF/` | Earlier ESP32 NDEF player |
| `esp32-display/` | Earlier TFT player |
| `esp32-ndef-tft/` | Previous GitHub TFT/NDEF snapshot, superseded by root firmware |
| `nanopc/` | NanoPC Python implementation |
| `rpi/` | Raspberry Pi Python implementations |

[Original project README](ORIGINAL_README.md) preserves legacy wiring/setup notes.
Old folder/sketch names may need adjustment to build with Arduino; these are source snapshots, not tested releases.

Embedded provisioning values have been replaced with `CONFIGURE_LOCALLY` and private certificate/key files omitted. Earlier commits still contain previously published material; deleting files here does not revoke credentials or remove Git history. Rotate affected credentials/keys separately.

`esp32/README.md` and `esp32/readme.md` differed only in case. Both are preserved, with the latter renamed `README-additional.md` for macOS/Windows compatibility.

`esp32-display/settings copy.h` was renamed `settings_copy.h` because Arduino IDE rejects the space-containing sketch filename, even when encountered in this archive. Contents are preserved.
