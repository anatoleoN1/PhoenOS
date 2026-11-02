#!/usr/bin/env python3
"""Genere tests/x509_vectors.h : une petite PKI de test (racines, intermediaires, feuilles) produite par la bibliotheque
"cryptography", et la liste des scenarios de verification avec le resultat attendu.
Usage : python3 tests/gen_x509_vectors.py > tests/x509_vectors.h"""
import datetime, calendar, ipaddress, warnings
warnings.filterwarnings("ignore")
from cryptography import x509
from cryptography.x509.oid import NameOID, ExtendedKeyUsageOID, ObjectIdentifier
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa, ec, ed25519

UTC = datetime.timezone.utc
def dt(y, m, d, h=0): return datetime.datetime(y, m, d, h, tzinfo=UTC)
def ts(y, m, d, h=0): return calendar.timegm(dt(y, m, d, h).timetuple())
NB, NA = dt(2026, 1, 1), dt(2027, 6, 1)
NOW = ts(2026, 10, 18, 12)
OK, PARSE, SIGALG, SIG, EXPIRED, NOTYET, HOST, UNTRUSTED, CA, EXT, CLOCK, KEYUSE = 0, -1, -2, -3, -4, -5, -6, -7, -8, -9, -10, -11

serial = [1000]
def mk(cn, pub, signer_key, issuer=None, nb=NB, na=NA, ca=None, pathlen=None, ku=None, san=None, eku=None, extra=(), hash_=hashes.SHA256(), with_cn=True):
    serial[0] += 1
    subj = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, cn)])
    b = x509.CertificateBuilder().subject_name(subj).issuer_name(issuer if issuer is not None else subj)\
        .public_key(pub).serial_number(serial[0]).not_valid_before(nb).not_valid_after(na)
    if ca is not None:
        b = b.add_extension(x509.BasicConstraints(ca=ca, path_length=pathlen), critical=True)
    if ku == "ca":
        b = b.add_extension(x509.KeyUsage(False, False, False, False, False, True, True, False, False), critical=True)
    elif ku == "sig":
        b = b.add_extension(x509.KeyUsage(True, False, False, False, False, False, False, False, False), critical=True)
    elif ku == "enc":
        b = b.add_extension(x509.KeyUsage(False, False, True, False, False, False, False, False, False), critical=True)
    if san:
        names = []
        for s in san:
            try: names.append(x509.IPAddress(ipaddress.ip_address(s)))
            except ValueError: names.append(x509.DNSName(s))
        b = b.add_extension(x509.SubjectAlternativeName(names), critical=False)
    if eku:
        b = b.add_extension(x509.ExtendedKeyUsage(eku), critical=False)
    for e, crit in extra:
        b = b.add_extension(e, critical=crit)
    alg = None if isinstance(signer_key, ed25519.Ed25519PrivateKey) else hash_
    c = b.sign(signer_key, alg)
    return c

K = lambda: ec.generate_private_key(ec.SECP256R1())
K384 = lambda: ec.generate_private_key(ec.SECP384R1())
KR = lambda bits=2048: rsa.generate_private_key(65537, bits)
SA = ["example.test", "*.wild.test", "10.1.2.3"]
SRV = [ExtendedKeyUsageOID.SERVER_AUTH]
certs, names = [], []
def add(name, c, key=None):
    certs.append(c); names.append(name); return len(certs) - 1
keys = {}

rk = K384(); root = mk("Test Root", rk.public_key(), rk, ca=True, ku="ca", hash_=hashes.SHA384()); keys["root"] = rk
R = add("root", root)
ik = KR(); inter = mk("Test Inter", ik.public_key(), rk, issuer=root.subject, ca=True, pathlen=0, ku="ca", hash_=hashes.SHA384()); I = add("inter", inter)
lk = K(); leaf_ec = mk("leaf-ec", lk.public_key(), ik, issuer=inter.subject, ca=False, ku="sig", san=SA, eku=SRV); LE = add("leaf_ec", leaf_ec)
lkr = KR(); leaf_rsa = mk("leaf-rsa", lkr.public_key(), ik, issuer=inter.subject, ca=False, ku="sig", san=["rsa.test"], eku=SRV); LR = add("leaf_rsa", leaf_rsa)
lke = ed25519.Ed25519PrivateKey.generate(); leaf_ed = mk("leaf-ed", lke.public_key(), ik, issuer=inter.subject, ca=False, san=["ed.test"], eku=SRV); LD = add("leaf_ed", leaf_ed)
# expiree / pas encore valide
leaf_old = mk("leaf-old", lk.public_key(), ik, issuer=inter.subject, nb=dt(2024, 1, 1), na=dt(2025, 6, 1), san=["example.test"], eku=SRV); LO = add("leaf_old", leaf_old)
leaf_future = mk("leaf-future", lk.public_key(), ik, issuer=inter.subject, nb=dt(2027, 1, 1), na=dt(2028, 1, 1), san=["example.test"], eku=SRV); LF = add("leaf_future", leaf_future)
# intermediaire qui n'est pas une autorite
nk = KR(); nonca = mk("Not CA", nk.public_key(), rk, issuer=root.subject, ca=False, hash_=hashes.SHA384()); NC = add("nonca", nonca)
leaf_nc = mk("leaf-nc", lk.public_key(), nk, issuer=nonca.subject, san=["example.test"], eku=SRV); LNC = add("leaf_nc", leaf_nc)
# longueur de chemin
i0k = K(); i0 = mk("Inter0", i0k.public_key(), rk, issuer=root.subject, ca=True, pathlen=0, ku="ca", hash_=hashes.SHA384()); I0 = add("inter_p0", i0)
i2k = K(); i2 = mk("Inter2", i2k.public_key(), i0k, issuer=i0.subject, ca=True, ku="ca"); I2 = add("inter2_under_p0", i2)
leaf_p = mk("leaf-p", lk.public_key(), i2k, issuer=i2.subject, san=["example.test"], eku=SRV); LP = add("leaf_pathlen", leaf_p)
i1k = K(); i1 = mk("Inter1", i1k.public_key(), rk, issuer=root.subject, ca=True, pathlen=1, ku="ca", hash_=hashes.SHA384()); I1 = add("inter_p1", i1)
i3k = K(); i3 = mk("Inter3", i3k.public_key(), i1k, issuer=i1.subject, ca=True, ku="ca"); I3 = add("inter3_under_p1", i3)
leaf_p1 = mk("leaf-p1", lk.public_key(), i3k, issuer=i3.subject, san=["example.test"], eku=SRV); LP1 = add("leaf_pathlen_ok", leaf_p1)
# extensions
leaf_crit = mk("leaf-crit", lk.public_key(), ik, issuer=inter.subject, san=["example.test"], eku=SRV, extra=[(x509.UnrecognizedExtension(ObjectIdentifier("1.2.3.4"), b"\x05\x00"), True)]); LCR = add("leaf_critical_unknown", leaf_crit)
leaf_ncr = mk("leaf-noncrit", lk.public_key(), ik, issuer=inter.subject, san=["example.test"], eku=SRV, extra=[(x509.UnrecognizedExtension(ObjectIdentifier("1.2.3.4"), b"\x05\x00"), False)]); LNCR = add("leaf_noncritical_unknown", leaf_ncr)
leaf_nosan = mk("example.test", lk.public_key(), ik, issuer=inter.subject, eku=SRV); LNS = add("leaf_no_san", leaf_nosan)
leaf_client = mk("leaf-client", lk.public_key(), ik, issuer=inter.subject, san=["example.test"], eku=[ExtendedKeyUsageOID.CLIENT_AUTH]); LCL = add("leaf_client_only", leaf_client)
leaf_enc = mk("leaf-enc", lk.public_key(), ik, issuer=inter.subject, ku="enc", san=["example.test"], eku=SRV); LEN = add("leaf_keyenc_only", leaf_enc)
k1024 = KR(1024); leaf_1024 = mk("leaf-1024", k1024.public_key(), ik, issuer=inter.subject, san=["example.test"], eku=SRV); L1K = add("leaf_rsa1024", leaf_1024)
try:
    leaf_sha1 = mk("leaf-sha1", lk.public_key(), ik, issuer=inter.subject, san=["example.test"], eku=SRV, hash_=hashes.SHA1()); LS1 = add("leaf_sha1", leaf_sha1)
except Exception: LS1 = None
# racine pareille mais autre cle (meme nom) : signature fausse
rk2 = K384(); root2 = mk("Test Root", rk2.public_key(), rk2, ca=True, ku="ca", hash_=hashes.SHA384()); R2 = add("root_same_name_other_key", root2)
# intermediaire expire
ie = mk("Test Inter", ik.public_key(), rk, issuer=root.subject, nb=dt(2023, 1, 1), na=dt(2025, 1, 1), ca=True, pathlen=0, ku="ca", hash_=hashes.SHA384()); IE = add("inter_expired", ie)
# racine Ed25519 qui signe directement une feuille
ek = ed25519.Ed25519PrivateKey.generate(); roote = mk("Ed Root", ek.public_key(), ek, ca=True, ku="ca"); RE = add("root_ed", roote)
lk2 = K(); leaf_e2 = mk("leaf-e2", lk2.public_key(), ek, issuer=roote.subject, san=["e2.test"], eku=SRV); LE2 = add("leaf_under_ed_root", leaf_e2)
# racine RSA 4096 / feuille ECDSA P-384 signee SHA-512
r4 = KR(4096); root4 = mk("RSA4096 Root", r4.public_key(), r4, ca=True, ku="ca", hash_=hashes.SHA512()); R4 = add("root_rsa4096", root4)
l384 = K384(); leaf_384 = mk("leaf-384", l384.public_key(), r4, issuer=root4.subject, san=["p384.test"], eku=SRV, hash_=hashes.SHA512()); L384 = add("leaf_p384_sha512", leaf_384)
# auto-signee, non fiable
sk = K(); selfs = mk("self.test", sk.public_key(), sk, ca=False, san=["self.test"], eku=SRV); SS = add("self_signed_leaf", selfs)
# alteration : un octet du TBS change (dans le nom de l'emetteur-sujet "leaf-ec")
d = bytearray(leaf_ec.public_bytes(serialization.Encoding.DER)); pos = d.find(b"leaf-ec"); d[pos + 5] ^= 0x01
tampered_idx = len(certs); certs.append(bytes(d)); names.append("leaf_tampered")

def der(c): return c if isinstance(c, bytes) else c.public_bytes(serialization.Encoding.DER)
out = ["/* GENERE par tests/gen_x509_vectors.py : ne pas modifier a la main. */"]
out.append("static const char *const X_CERT[] = {")
for n, c in zip(names, certs): out.append('    "%s",   /* %d %s */' % (der(c).hex(), len(out) - 2, n))
out.append("};")
out.append("struct v_chain { const char *name; int n; int idx[5]; const char *host; u32 now; int nroots; int roots[3]; int want; };")
out.append("static const struct v_chain V_CHAIN[] = {")
def sc(name, chain, host, want, roots=(R,), now=NOW):
    out.append('    { "%s", %d, {%s}, "%s", %du, %d, {%s}, %d },' % (name, len(chain), ", ".join(map(str, list(chain) + [0] * (5 - len(chain)))), host, now, len(roots), ", ".join(map(str, list(roots) + [0] * (3 - len(roots)))), want))
sc("chaine complete", [LE, I], "example.test", OK)
sc("intermediaire manquant", [LE], "example.test", UNTRUSTED)
sc("racine envoyee en plus", [LE, I, R], "example.test", OK)
sc("ordre inverse", [I, LE], "example.test", HOST)       # la premiere est l'intermediaire : pas le bon nom
sc("feuille puis intermediaire inutile", [LE, LR, I], "example.test", OK)
sc("nom en majuscules", [LE, I], "EXAMPLE.TEST", OK)
sc("nom different", [LE, I], "other.test", HOST)
sc("joker une etiquette", [LE, I], "a.wild.test", OK)
sc("joker sans etiquette", [LE, I], "wild.test", HOST)
sc("joker deux etiquettes", [LE, I], "a.b.wild.test", HOST)
sc("adresse IP du SAN", [LE, I], "10.1.2.3", OK)
sc("autre adresse IP", [LE, I], "10.1.2.4", HOST)
sc("point final", [LE, I], "example.test.", OK)
sc("feuille RSA", [LR, I], "rsa.test", OK)
sc("feuille Ed25519", [LD, I], "ed.test", OK)
sc("feuille expiree", [LO, I], "example.test", EXPIRED)
sc("feuille pas encore valide", [LF, I], "example.test", NOTYET)
sc("horloge trop ancienne", [LE, I], "example.test", CLOCK, now=ts(2020, 1, 1))
sc("apres expiration de la feuille", [LE, I], "example.test", EXPIRED, now=ts(2027, 7, 1))
sc("intermediaire pas autorite", [LNC, NC], "example.test", CA)
sc("longueur de chemin depassee", [LP, I2, I0], "example.test", CA)
sc("longueur de chemin respectee", [LP1, I3, I1], "example.test", OK)
sc("extension critique inconnue", [LCR, I], "example.test", EXT)
sc("extension non critique inconnue", [LNCR, I], "example.test", OK)
sc("pas de SAN", [LNS, I], "example.test", HOST)
sc("usage client seulement", [LCL, I], "example.test", KEYUSE)
sc("keyUsage sans signature", [LEN, I], "example.test", KEYUSE)
sc("RSA 1024 bits refuse", [L1K, I], "example.test", SIGALG)
if LS1 is not None: sc("signature SHA-1 refusee", [LS1, I], "example.test", SIGALG)
sc("racine de meme nom, autre cle", [LE, I], "example.test", SIG, roots=(R2,))
sc("deux racines dont la bonne", [LE, I], "example.test", OK, roots=(R2, R))
sc("intermediaire expire", [LE, IE], "example.test", EXPIRED)
sc("racine Ed25519", [LE2], "e2.test", OK, roots=(RE,))
sc("racine RSA 4096, feuille P-384 SHA-512", [L384], "p384.test", OK, roots=(R4,))
sc("auto-signee non fiable", [SS], "self.test", UNTRUSTED)
sc("feuille alteree", [tampered_idx, I], "example.test", SIG)
sc("aucune racine", [LE, I], "example.test", UNTRUSTED, roots=())
out.append("};")
print("\n".join(out))
