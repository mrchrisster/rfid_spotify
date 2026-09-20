#!/usr/bin/env python3
"""Compare real firmware against an isolated HTTPS/PKCE link probe. Never uploads."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cli", default="/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli")
parser.add_argument("--fqbn", default="esp32:esp32:esp32")
parser.add_argument("--output", type=Path, default=root / "experiments/https_flash/measurement.json")
args = parser.parse_args()
working = Path(tempfile.mkdtemp(prefix="spotify-https-flash-"))
sketch = working / root.name
sketch.mkdir()
# Use the exact source/configuration of the current firmware for an apples-to-apples baseline.
# Copies and build outputs remain in an owner-only temporary directory, including local credentials.
for source in root.iterdir():
    if source.is_file() and source.suffix in (".ino", ".h", ".cpp"):
        shutil.copy2(source, sketch / source.name)

measurements = {"fqbn": args.fqbn, "temporary_directory": str(working), "builds": {}}
def build(name, display=True, fqbn=None):
    log = working / (name + ".log")
    path = working / ("build-" + name)
    print("Building " + name + "…", flush=True)
    with log.open("w") as output:
        result = subprocess.run([args.cli, "compile", "--fqbn", fqbn or args.fqbn, "--build-path", str(path),
                                 "--build-property", "compiler.cpp.extra_flags=-DPLAYER_HAS_DISPLAY=" + str(int(display)),
                                 "--warnings", "all", str(sketch)], stdout=output, stderr=subprocess.STDOUT)
    text = log.read_text()
    flash = re.search(r"Sketch uses (\d+) bytes.*Maximum is (\d+) bytes", text)
    ram = re.search(r"Global variables use (\d+) bytes.*Maximum is (\d+) bytes", text)
    data = {"exit_code": result.returncode, "log": str(log), "build_directory": str(path), "display": display, "fqbn": fqbn or args.fqbn}
    if flash:
        data.update(flash_bytes=int(flash[1]), partition_bytes=int(flash[2]), remaining_bytes=int(flash[2])-int(flash[1]))
    if ram:
        data.update(static_ram_bytes=int(ram[1]), ram_total_bytes=int(ram[2]))
    measurements["builds"][name] = data
    print(json.dumps(data), flush=True)
    if result.returncode and not flash:
        print("Build failed before size report; inspect " + str(log), flush=True)
    return result.returncode

baseline = build("baseline")
if not baseline or "flash_bytes" in measurements["builds"]["baseline"]:
    build("baseline-screenless", display=False)
    # A throwaway ECDSA leaf certificate models actual certificate/key storage.
    # It is not a trusted deployment certificate and never leaves the temporary directory.
    config = working / "certificate.cnf"
    config.write_text("[req]\ndistinguished_name=dn\nx509_extensions=extensions\nprompt=no\n"
                      "[dn]\nCN=spotify-player.local\n[extensions]\nsubjectAltName=DNS:spotify-player.local\n"
                      "basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\nextendedKeyUsage=serverAuth\n")
    with (working / "openssl.log").open("w") as output:
        subprocess.run(["openssl", "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256", "-nodes",
                        "-days", "30", "-config", str(config), "-keyout", str(working / "probe.key"),
                        "-out", str(working / "probe.crt")], check=True, stdout=output, stderr=subprocess.STDOUT)
    cert = (working / "probe.crt").read_text()
    key = (working / "probe.key").read_text()
    (sketch / "ProbeCertificate.h").write_text('#pragma once\nstatic const char probeCertificate[] = R"PEM(' + cert + ')PEM";\n'
        + 'static const char probePrivateKey[] = R"PEM(' + key + ')PEM";\n')
    for filename in ("HttpsFlashProbe.h", "HttpsFlashProbe.cpp"):
        shutil.copy2(Path(__file__).parent / filename, sketch / filename)
    ino = sketch / (root.name + ".ino")
    content = ino.read_text()
    content = '#include "HttpsFlashProbe.h"\n' + content
    content = content.replace('webServer.close(); webServer.begin(); logMessage(',
                              'webServer.close(); webServer.begin(); startHttpsFlashProbe(); logMessage(', 1)
    ino.write_text(content)
    build("https-pkce")
    build("https-pkce-screenless", display=False)
    if args.fqbn in ("esp32:esp32:esp32", "esp32:esp32:esp32c6"):
        build("https-pkce-larger-partition", fqbn=args.fqbn + ":PartitionScheme=min_spiffs")
report = args.output
report.write_text(json.dumps(measurements, indent=2) + "\n")
print("Measurements saved to " + str(report), flush=True)
