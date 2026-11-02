#!/usr/bin/env python3
"""Genere src/roots.c : la liste des autorites de certification racines embarquees dans PhoenOS.
Source : un fichier PEM de racines (par defaut le lot Mozilla fourni par Debian/Ubuntu, /etc/ssl/certs/ca-certificates.crt).
On ne garde que les autorites listees ci-dessous (les plus utilisees sur le web) et dont la cle est prise en charge
(RSA >= 2048 bits, ECDSA P-256/P-384). Ajouter un nom a la liste puis relancer pour en embarquer d'autres.
Usage : python3 tools/gen_roots.py [lot.pem] > src/roots.c"""
import re, sys, datetime, warnings
warnings.filterwarnings("ignore")
from cryptography import x509
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa, ec

WANTED = """ISRG Root X1|ISRG Root X2|DigiCert Global Root G2|DigiCert Global Root G3|DigiCert Assured ID Root G2|DigiCert Assured ID Root G3
DigiCert Trusted Root G4|DigiCert TLS ECC P384 Root G5|DigiCert TLS RSA4096 Root G5|GTS Root R1|GTS Root R3|GTS Root R4
GlobalSign Root R46|GlobalSign Root E46|USERTrust RSA Certification Authority|USERTrust ECC Certification Authority
Sectigo Public Server Authentication Root R46|Sectigo Public Server Authentication Root E46|COMODO RSA Certification Authority
COMODO ECC Certification Authority|Amazon Root CA 1|Amazon Root CA 2|Amazon Root CA 3|Amazon Root CA 4
Starfield Root Certificate Authority - G2|Starfield Services Root Certificate Authority - G2|Go Daddy Root Certificate Authority - G2
Microsoft RSA Root Certificate Authority 2017|Microsoft ECC Root Certificate Authority 2017|IdenTrust Commercial Root CA 1
SSL.com TLS RSA Root CA 2022|SSL.com TLS ECC Root CA 2022|Certum Trusted Root CA|Certum EC-384 CA|HARICA TLS RSA Root CA 2021|HARICA TLS ECC Root CA 2021""".replace("\n", "|").split("|")
OU_WANTED = ["GlobalSign Root CA - R3", "GlobalSign ECC Root CA - R4", "GlobalSign ECC Root CA - R5", "GlobalSign Root CA - R6"]   # nom dans OU

path = sys.argv[1] if len(sys.argv) > 1 else "/etc/ssl/certs/ca-certificates.crt"
pem = open(path).read()
certs = [x509.load_pem_x509_certificate(m.encode()) for m in re.findall(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", pem, re.S)]
def names(c):
    out = []
    for a in c.subject:
        out.append(a.value)
    return out
chosen, found = [], set()
for c in certs:
    nm = names(c)
    if any("Anthropic" in v for v in nm):
        continue
    key = next((w for w in WANTED + OU_WANTED if w in nm), None)
    if not key:
        continue
    pk = c.public_key()
    ok = (isinstance(pk, rsa.RSAPublicKey) and pk.key_size >= 2048) or (isinstance(pk, ec.EllipticCurvePublicKey) and pk.curve.name in ("secp256r1", "secp384r1"))
    if not ok:
        sys.stderr.write("ignoree (cle non prise en charge) : %s\n" % key); continue
    chosen.append((key, c)); found.add(key)
for w in WANTED + OU_WANTED:
    if w not in found:
        sys.stderr.write("absente du lot : %s\n" % w)
chosen.sort(key=lambda t: t[0])
print("/* GENERE par tools/gen_roots.py : ne pas modifier a la main.")
print(" * %d autorites racines (extrait du lot Mozilla du %s). A regenerer (et publier une mise a jour) de temps en temps. */" % (len(chosen), datetime.date.today().isoformat()))
print('#include "types.h"\n')
for i, (name, c) in enumerate(chosen):
    der = c.public_bytes(serialization.Encoding.DER)
    print("/* %s (expire %s) */" % (name, c.not_valid_after.date()))
    print("static const u8 R%d[%d] = {" % (i, len(der)))
    for k in range(0, len(der), 16):
        print("    " + ",".join("0x%02x" % b for b in der[k:k+16]) + ",")
    print("};")
print("const u8 *const roots_der[%d] = {%s};" % (len(chosen), ", ".join("R%d" % i for i in range(len(chosen)))))
print("const u32 roots_len[%d] = {%s};" % (len(chosen), ", ".join("sizeof R%d" % i for i in range(len(chosen)))))
print("const int roots_count = %d;" % len(chosen))
