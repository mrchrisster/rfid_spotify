import hashlib
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
class ProvisionTests(unittest.TestCase):
    def test_private_certificate_and_renewal_preserve_ca(self):
        with tempfile.TemporaryDirectory() as temp:
            work=Path(temp)
            args=[sys.executable,str(ROOT/'tools/provision_https.py'),'--directory',str(work/'private'),
                  '--header',str(work/'DeviceCertificate.h'),'--hostname','rfid-test']
            subprocess.run(args,check=True,stdout=subprocess.DEVNULL)
            header=work/'DeviceCertificate.h'; ca=work/'private/ca.crt'
            fingerprint=hashlib.sha256(ca.read_bytes()).digest()
            self.assertEqual(stat.S_IMODE(header.stat().st_mode),0o600)
            self.assertIn('deviceCertificateExpiresUnix',header.read_text())
            self.assertIn('rfid-test',header.read_text())
            self.assertIn('DEVICE_AUTO_CERTIFICATE 1',header.read_text())
            cert_text=subprocess.check_output(['openssl','x509','-in',str(work/'private/device.crt'),'-noout','-text'],text=True)
            self.assertIn('ASN1 OID: prime256v1',cert_text)
            self.assertNotIn((work/'private/ca.key').read_text(),header.read_text())
            subprocess.run(['openssl','verify','-CAfile',str(ca),'-untrusted',str(work/'private/issuer.crt'),str(work/'private/device.crt')],check=True,stdout=subprocess.DEVNULL)
            duplicate=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            self.assertNotEqual(duplicate.returncode,0)
            subprocess.run(args+['--renew'],check=True,stdout=subprocess.DEVNULL)
            self.assertEqual(hashlib.sha256(ca.read_bytes()).digest(),fingerprint)
            bad=subprocess.run(args+['--hostname','bad.local'],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            self.assertNotEqual(bad.returncode,0)

    def test_delegated_key_cannot_sign_other_hosts(self):
        with tempfile.TemporaryDirectory() as temp:
            work=Path(temp)
            subprocess.run([sys.executable,str(ROOT/'tools/provision_https.py'),'--directory',str(work),
                            '--header',str(work/'identity.h'),'--hostname','rfid-test'],check=True,stdout=subprocess.DEVNULL)
            # Model theft/misuse of only the on-device signing key. A browser verifier
            # must reject a valid signature on a certificate outside its DNS subtree.
            config=work/'wrong.cnf'
            config.write_text('[req]\nprompt=no\ndistinguished_name=dn\n[dn]\nCN=other.local\n'
                              '[extensions]\nsubjectAltName=DNS:other.local\nbasicConstraints=CA:FALSE\n')
            subprocess.run(['openssl','req','-new','-key',str(work/'device.key'),'-config',str(config),
                            '-out',str(work/'wrong.csr')],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
            subprocess.run(['openssl','x509','-req','-in',str(work/'wrong.csr'),'-CA',str(work/'issuer.crt'),
                            '-CAkey',str(work/'issuer.key'),'-CAcreateserial','-days','100','-extfile',str(config),
                            '-extensions','extensions','-out',str(work/'wrong.crt')],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
            verified=subprocess.run(['openssl','verify','-CAfile',str(work/'ca.crt'),'-untrusted',str(work/'issuer.crt'),
                                     str(work/'wrong.crt')],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            self.assertNotEqual(verified.returncode,0)
            self.assertIn(b'permitted subtree',verified.stdout+verified.stderr)
