#!/usr/bin/env python3
"""Run native regression tests with real production code, fake I/O, and sanitizers."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--arduino-json", type=Path, default=Path.home() / "Documents/Arduino/libraries/ArduinoJson/src")
args = parser.parse_args()
if not (args.arduino_json / "ArduinoJson.h").is_file():
    parser.error("Supply --arduino-json pointing to ArduinoJson 7.4.3 src directory")
env = dict(os.environ, UBSAN_OPTIONS="halt_on_error=1", ASAN_OPTIONS="abort_on_error=1")
with tempfile.TemporaryDirectory(prefix="spotify-tests-") as temporary:
    for name, sources, extra in [
        ("core", ["tests/test_core.cpp"], []),
        ("oauth", ["tests/test_oauth.cpp"], []),
        ("certificate", ["tests/test_certificate_policy.cpp"], []),
        ("rfid", ["tests/test_rfid.cpp"], []),
        ("rfid_presence", ["tests/test_rfid_presence.cpp"], []),
        ("spotify", ["tests/test_spotify.cpp", "SpotifyClient.cpp"], [
            "-DARDUINOJSON_ENABLE_ARDUINO_STRING=1", "-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0",
            "-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0", "-DARDUINOJSON_ENABLE_PROGMEM=0", "-I" + str(args.arduino_json),
        ]),
        ("device_auth", ["tests/test_device_auth.cpp", "SpotifyClient.cpp"], [
            "-Itests/fakes/https", "-DARDUINOJSON_ENABLE_ARDUINO_STRING=1", "-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0",
            "-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0", "-DARDUINOJSON_ENABLE_PROGMEM=0", "-I" + str(args.arduino_json),
        ]),
        ("device_auth_password", ["tests/test_device_auth.cpp", "SpotifyClient.cpp"], [
            "-Itests/fakes/https", "-DPLAYER_REQUIRE_WEB_AUTH=1", "-DARDUINOJSON_ENABLE_ARDUINO_STRING=1", "-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0",
            "-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0", "-DARDUINOJSON_ENABLE_PROGMEM=0", "-I" + str(args.arduino_json),
        ]),
    ]:
        output = str(Path(temporary) / name)
        subprocess.run(["clang++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-g",
                        "-Itests/fakes", "-I.", *extra, *sources, "-o", output], cwd=root, check=True, env=env)
        subprocess.run([output], cwd=root, check=True, env=env)
subprocess.run([sys.executable, "-m", "unittest", "discover", "-s", "tests", "-p", "test_*.py", "-v"], cwd=root, check=True)

subprocess.run(['node', 'tests/test_dashboard.js'], cwd=root, check=True)
