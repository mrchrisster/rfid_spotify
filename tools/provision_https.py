#!/usr/bin/env python3
"""Create a private local CA and a player HTTPS certificate. Does not upload or install trust."""
import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--hostname', default='spotify-player', help='Unique mDNS name, without .local')
    parser.add_argument('--directory', type=Path, default=ROOT / 'https-auto-private')
    parser.add_argument('--header', type=Path, default=ROOT / 'DeviceCertificate.h')
    parser.add_argument('--replace-header', action='store_true', help='Replace the build header when provisioning another device/CA; new browser trust is required')
    parser.add_argument('--renew', action='store_true', help='Replace leaf certificate/header, retaining the existing CA')
    args = parser.parse_args()
    if not re.fullmatch(r'[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?', args.hostname):
        parser.error('Hostname must be a lowercase DNS label (no .local suffix).')
    directory = args.directory.resolve()
    header = args.header.resolve()
    if ((header.exists() and not args.replace_header) or (directory / 'device.crt').exists()) and not args.renew:
        parser.error('Certificate already exists; use --renew to retain the CA and replace the leaf.')
    os.umask(0o077)
    directory.mkdir(parents=True, exist_ok=True, mode=0o700)
    os.chmod(directory, 0o700)
    def run(*arguments):
        subprocess.run(['openssl', *map(str, arguments)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    ca_key, ca_cert = directory / 'ca.key', directory / 'ca.crt'
    if ca_key.exists() != ca_cert.exists():
        parser.error('Incomplete CA files; restore the matching key and certificate before continuing.')
    if not ca_key.exists():
        if args.renew:
            parser.error('Renewal requires the original CA files to preserve browser trust.')
        ca_config = directory / 'ca.cnf'
        ca_config.write_text('[req]\nprompt=no\ndistinguished_name=dn\nx509_extensions=extensions\n'
                             '[dn]\nCN=RFID Player Private Root\n[extensions]\nbasicConstraints=critical,CA:TRUE,pathlen:1\n'
                             'keyUsage=critical,keyCertSign,cRLSign\nsubjectKeyIdentifier=hash\n')
        run('req', '-x509', '-newkey', 'ec', '-pkeyopt', 'ec_paramgen_curve:P-256', '-pkeyopt', 'ec_param_enc:named_curve', '-nodes', '-days', '3650',
            '-config', ca_config, '-keyout', ca_key, '-out', ca_cert)
    # Delegate only this device's DNS namespace. Root signing key stays off-device.
    # Name constraints are noncritical because ESP-IDF's bundled mbedTLS cannot
    # parse critical nameConstraints when loading the server chain. Browser
    # validators (and OpenSSL tests below) enforce the constraint regardless.
    issuer_key, issuer_csr, issuer_cert = (directory / name for name in ('issuer.key', 'issuer.csr', 'issuer.crt'))
    if not issuer_cert.exists():
        if args.renew:
            parser.error('This is a legacy/manual CA directory. Use a new directory for automatic renewal and install its new ca.crt once.')
        issuer_config = directory / 'issuer.cnf'
        issuer_config.write_text('[req]\nprompt=no\ndistinguished_name=dn\n[dn]\nCN=' + args.hostname + ' Device Issuer\n'
            '[extensions]\nbasicConstraints=critical,CA:TRUE,pathlen:0\nkeyUsage=critical,keyCertSign,cRLSign\n'
            'subjectKeyIdentifier=hash\nauthorityKeyIdentifier=keyid,issuer\n'
            'nameConstraints=permitted;DNS:' + args.hostname + '.local,excluded;IP:0.0.0.0/0.0.0.0,excluded;IP:0:0:0:0:0:0:0:0/0:0:0:0:0:0:0:0\n')
        run('req', '-new', '-newkey', 'ec', '-pkeyopt', 'ec_paramgen_curve:P-256', '-pkeyopt', 'ec_param_enc:named_curve', '-nodes', '-config', issuer_config,
            '-keyout', issuer_key, '-out', issuer_csr)
        run('x509', '-req', '-in', issuer_csr, '-CA', ca_cert, '-CAkey', ca_key, '-CAcreateserial', '-days', '3649',
            '-sha256', '-extfile', issuer_config, '-extensions', 'extensions', '-out', issuer_cert)
    elif not issuer_key.exists():
        parser.error('Missing device issuer key. Restore it before renewing.')
    # Check delegation matches the requested hostname before replacing any leaf.
    issuer_text = subprocess.check_output(['openssl', 'x509', '-in', str(issuer_cert), '-noout', '-text'], text=True)
    if 'DNS:' + args.hostname + '.local' not in issuer_text:
        parser.error('The existing issuer belongs to another hostname. Use a separate directory.')
    config = directory / 'device.cnf'
    config.write_text('[req]\nprompt=no\ndistinguished_name=dn\n[dn]\nCN=' + args.hostname + '.local\n'
                      '[extensions]\nsubjectAltName=DNS:' + args.hostname + '.local\n'
                      'basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\nextendedKeyUsage=serverAuth\n')
    key, csr, cert = (directory / name for name in ('device.key', 'device.csr', 'device.crt'))
    run('req', '-new', '-newkey', 'ec', '-pkeyopt', 'ec_paramgen_curve:P-256', '-pkeyopt', 'ec_param_enc:named_curve', '-nodes', '-config', config,
        '-keyout', key, '-out', csr)
    run('x509', '-req', '-in', csr, '-CA', issuer_cert, '-CAkey', issuer_key, '-CAcreateserial', '-days', '397',
        '-sha256', '-extfile', config, '-extensions', 'extensions', '-out', cert)
    run('verify', '-CAfile', ca_cert, '-untrusted', issuer_cert, cert)
    enddate = subprocess.check_output(['openssl', 'x509', '-in', str(cert), '-noout', '-enddate'], text=True).strip().split('=', 1)[1]
    expiry = int(datetime.strptime(enddate, '%b %d %H:%M:%S %Y %Z').replace(tzinfo=timezone.utc).timestamp())
    content = '#pragma once\nstatic const char deviceHostname[] = "' + args.hostname + '";\n'
    content += 'static const int64_t deviceCertificateExpiresUnix = ' + str(expiry) + 'LL;\n'
    content += 'static const char deviceCertificate[] = R"PEM(' + cert.read_text() + ')PEM";\n'
    content += '#define DEVICE_AUTO_CERTIFICATE 1\n'
    content += 'static const char deviceIssuerCertificate[] = R"PEM(' + issuer_cert.read_text() + ')PEM";\n'
    content += 'static const char deviceIssuerPrivateKey[] = R"PEM(' + issuer_key.read_text() + ')PEM";\n'
    content += 'static const char devicePrivateKey[] = R"PEM(' + key.read_text() + ')PEM";\n'
    temp = header.with_suffix('.tmp')
    with temp.open('w') as output:
        os.chmod(temp, 0o600)
        output.write(content)
    temp.replace(header)
    print('Created private firmware header:', header)
    print('Install ONLY this public CA certificate on your browser/phone:', ca_cert)
    print('Register this exact Spotify redirect URI: https://' + args.hostname + '.local/callback')
    print('The device renews its 397-day HTTPS certificate automatically, 90 days before expiry.')
    print('The signing authority lasts about ten years; replacing it will require browser trust setup again.')
    print('Keep all .key files private. The root ca.key stays off-device; only the constrained issuer key is embedded. No trust settings changed and no firmware uploaded.')

if __name__ == '__main__':
    main()
