#!/usr/bin/env python3
"""Build a named board/reader/display variant; outputs contain local credentials."""
import argparse
from pathlib import Path
import shutil
import subprocess

PROFILES = {
    'esp32-mfrc522-tft': (1, 'esp32'),
    'esp32-mfrc522-headless': (2, 'esp32'),
    'esp32c6-pn532-headless': (3, 'esp32c6'),
}
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('profile', choices=PROFILES)
parser.add_argument('--cli', default='arduino-cli')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
number, board = PROFILES[args.profile]
out = root / 'build' / args.profile
subprocess.run([args.cli, 'compile', '--fqbn', f'esp32:esp32:{board}:PartitionScheme=min_spiffs',
                '--build-property', f'compiler.cpp.extra_flags=-DPLAYER_HARDWARE_PROFILE={number}',
                '--build-path', str(out), str(root)], check=True)
source = out / (root.name + '.ino.bin')
target = out / (args.profile + '.bin')
shutil.copyfile(source, target)
print(f'Application OTA file: {target}\nPrivate build: do not publish credential-bearing binaries.')
