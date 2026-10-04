import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
class Profiles(unittest.TestCase):
    def preprocess(self, *defines):
        return subprocess.run(['clang++', '-E', '-P', '-x', 'c++', '-I', str(ROOT),
                               *['-D'+d for d in defines], '-'],
                              input='#include "HardwareProfile.h"\nPLAYER_PROFILE_NAME\nPLAYER_HAS_DISPLAY\nPLAYER_READER_PN532\n',
                              capture_output=True, text=True)
    def test_profiles(self):
        for number, chip, name, display, reader in [(1,'ESP32','esp32-mfrc522-tft',1,0),
              (2,'ESP32','esp32-mfrc522-headless',0,0),(3,'ESP32C6','esp32c6-pn532-headless',0,1)]:
            p=self.preprocess('ARDUINO_ARCH_ESP32',f'CONFIG_IDF_TARGET_{chip}',f'PLAYER_HARDWARE_PROFILE={number}')
            self.assertEqual(p.returncode,0,p.stderr)
            self.assertIn(f'"{name}"\n{display}\n{reader}',p.stdout)
    def test_mismatches(self):
        for defines in [('PLAYER_HARDWARE_PROFILE=3','CONFIG_IDF_TARGET_ESP32'),
                        ('PLAYER_HARDWARE_PROFILE=1','CONFIG_IDF_TARGET_ESP32C6'),
                        ('PLAYER_HARDWARE_PROFILE=3','PLAYER_HAS_DISPLAY=1','CONFIG_IDF_TARGET_ESP32C6'),
                        ('PLAYER_HARDWARE_PROFILE=99',)]:
            self.assertNotEqual(self.preprocess('ARDUINO_ARCH_ESP32',*defines).returncode,0)
    def test_legacy_headless(self):
        p=self.preprocess('PLAYER_HAS_DISPLAY=0')
        self.assertEqual(p.returncode,0)
        self.assertIn('"esp32-mfrc522-headless"',p.stdout)
