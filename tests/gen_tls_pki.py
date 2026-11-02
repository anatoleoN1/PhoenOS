#!/usr/bin/env python3
"""Cree dans le dossier donne la PKI utilisee par le test TLS (tests/test_tls.c) : une autorite, un intermediaire, des certificats
serveur de plusieurs types (ECDSA P-256 / P-384, RSA, Ed25519), un certificat expire, un mauvais nom, une seconde autorite.
Usage : python3 tests/gen_tls_pki.py DOSSIER"""
import sys, os, datetime, warnings
warnings.filterwarnings("ignore")
from cryptography import x509
from cryptography.x509.oid import NameOID, ExtendedKeyUsageOID
from cryptography.hazmat.primitives import hashes, serialization as S
from cryptography.hazmat.primitives.asymmetric import rsa, ec, ed25519
out = sys.argv[1]; os.makedirs(out, exist_ok=True)
now = datetime.datetime.now(datetime.timezone.utc)
day = datetime.timedelta(days=1)
n = [100]
def name(cn): return x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, cn)])
def cert(cn, key, signer, issuer, ca=False, sans=None, nb=None, na=None, pathlen=None):
    n[0] += 1
    b = x509.CertificateBuilder().subject_name(name(cn)).issuer_name(issuer.subject if issuer is not None else name(cn))\
        .public_key(key.public_key()).serial_number(n[0]).not_valid_before(nb or now - day).not_valid_after(na or now + 30 * day)
    b = b.add_extension(x509.BasicConstraints(ca=ca, path_length=pathlen), critical=True)
    if ca:
        b = b.add_extension(x509.KeyUsage(False, False, False, False, False, True, True, False, False), critical=True)
    else:
        b = b.add_extension(x509.KeyUsage(True, False, False, False, False, False, False, False, False), critical=True)
        b = b.add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
        b = b.add_extension(x509.SubjectAlternativeName([x509.DNSName(s) for s in sans]), critical=False)
    alg = None if isinstance(signer, ed25519.Ed25519PrivateKey) else hashes.SHA256()
    return b.sign(signer, alg)
def save(base, c, k):
    open(os.path.join(out, base + ".pem"), "wb").write(c.public_bytes(S.Encoding.PEM))
    if k is not None:
        open(os.path.join(out, base + ".key"), "wb").write(k.private_bytes(S.Encoding.PEM, S.PrivateFormat.PKCS8, S.NoEncryption()))
cak = ec.generate_private_key(ec.SECP384R1()); ca = cert("PhoenOS Test CA", cak, cak, None, ca=True); save("ca", ca, cak)
ik = ec.generate_private_key(ec.SECP256R1()); inter = cert("PhoenOS Test Inter", ik, cak, ca, ca=True, pathlen=0); save("inter", inter, ik)
S_OK = ["tls.test"]
for base, key in (("srv_ec", ec.generate_private_key(ec.SECP256R1())), ("srv_p384", ec.generate_private_key(ec.SECP384R1())),
                  ("srv_rsa", rsa.generate_private_key(65537, 2048)), ("srv_ed", ed25519.Ed25519PrivateKey.generate())):
    save(base, cert(base, key, ik if base == "srv_ec" else cak, inter if base == "srv_ec" else ca, sans=S_OK), key)
k = ec.generate_private_key(ec.SECP256R1()); save("srv_expired", cert("exp", k, cak, ca, sans=S_OK, nb=now - 400 * day, na=now - 300 * day), k)
k = ec.generate_private_key(ec.SECP256R1()); save("srv_wrongname", cert("wrong", k, cak, ca, sans=["other.test"]), k)
k2 = ec.generate_private_key(ec.SECP256R1()); ca2 = cert("PhoenOS Other CA", k2, k2, None, ca=True); save("ca2", ca2, k2)
k = ec.generate_private_key(ec.SECP256R1()); save("srv_other_ca", cert("o", k, k2, ca2, sans=S_OK), k)
# la CA de confiance du test, en DER, pour le programme
open(os.path.join(out, "ca.der"), "wb").write(ca.public_bytes(S.Encoding.DER))
open(os.path.join(out, "ca2.der"), "wb").write(ca2.public_bytes(S.Encoding.DER))
