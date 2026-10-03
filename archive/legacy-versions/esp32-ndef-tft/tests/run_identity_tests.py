#!/usr/bin/env python3
"""Test real firmware certificate generation with a local Mbed TLS 3.6.6 source/build tree."""
import argparse
from pathlib import Path
import os
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--mbedtls',required=True,type=Path)
a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='rfid-identity-tests-') as temp:
    binary=Path(temp)/'identity';chain=Path(temp)/'chain.pem'
    subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g',
                    '-Itests/fakes/identity','-Itests/fakes','-I.','-I'+str(a.mbedtls/'include'),
                    'tests/test_device_identity.cpp',str(a.mbedtls/'build/library/libmbedx509.a'),
                    str(a.mbedtls/'build/library/libmbedcrypto.a'),'-o',str(binary)],cwd=root,check=True)
    subprocess.run([str(binary),str(chain)],cwd=root,check=True,
                   env=dict(os.environ,UBSAN_OPTIONS='halt_on_error=1',ASAN_OPTIONS='abort_on_error=1'))
    subprocess.run(['openssl','verify','-CAfile',str(root/'https-auto-private/ca.crt'),
                    '-untrusted',str(root/'https-auto-private/issuer.crt'),str(chain)],check=True)
