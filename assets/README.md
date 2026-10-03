# Startup logo

`player-logo-source.png` is the selected image edit derived from the user's supplied
JPEG. The built-in image-generation tool was used, not the CLI. The first two
transparent variants were discarded due to typography/alpha artifacts. The
selected edit replaces the texture with a flat navy background; it is not a
transparent PNG or a pixel-identical extraction of the original JPEG.

Final edit prompt:

> Edit target: original attached logo. Replace ONLY the textured background with a perfectly flat, uniform dark navy RGB(16,28,33), hex #101c21. OPAQUE background, no transparency, no texture, no vignette, no shadow. Preserve the original smiling boy teal hair/headphones, white face and white outline, black internal lines, and BOTH lines of plain white text SPOTIFY / RFID CARD PLAYER without added outlines or sticker border. Preserve original logo proportions and spacing faithfully. Crop fairly tightly to the complete logo with just modest margin. This is a clean flat bitmap asset for an embedded TFT, not a photograph or mockup. Do not add any elements.

`tools/encode_player_logo.py` resizes and converts the selected asset to RGB565,
normalizes very dark pixels to screen navy, and writes `PlayerLogo.h`. The bitmap
is 106x144 (30,528 bytes) and resides in program flash. Pillow is needed only to
regenerate the asset, not to compile or run the firmware.

`player-logo-tft.png` is an exact RGB565 round-trip of the stored bitmap.
`startup-preview.png` illustrates the ready state at 320x240 using the same
coordinates, colors, and Adafruit classic bitmap font as the firmware. It is a
software preview, not a photograph of the physical TFT; the sample IP is replaced
by the actual device IP at runtime. Physical display verification is still needed.

## Larger character with separate text

The current screen uses `player-head-source.png`, edited with the built-in image
tool. The previous full-logo asset remains available for reference. Final prompt:

> Edit this supplied logo for an embedded screen. Keep ONLY the smiling boy's head with teal hair and teal headphones, preserving original white face, white external outline, black lines and original illustration design faithfully. Remove ALL text below the head, do not replace it. Replace textured background with uniform flat opaque dark navy #101c21 RGB(16,28,33). No transparency or texture or vignette. Tight framing around head and headphones with a small margin, approximately square canvas. No other elements. The lettering will be drawn separately by firmware.

The current encoded head is 138x144 (39,744 bytes), placed at the top right.
Firmware draws SPOTIFY / RFID CARD / PLAYER separately to its left at larger text
sizes. `startup-preview.png` now shows this layout with German prompts. The
selector supports English, German, French and Spanish; German is the default
when no valid saved language exists. This supersedes the dimensions and layout
of the first full-logo version described above.
