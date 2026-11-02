#!/usr/bin/env python3
"""Genere tests/pk_vectors.h : signatures RSA (PKCS#1 v1.5, PSS) et ECDSA produites par la bibliotheque "cryptography".
Usage : python3 tests/gen_pk_vectors.py > tests/pk_vectors.h   (les cles RSA sont tirees au hasard : le fichier change a chaque fois)"""
import hashlib
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import rsa, ec, padding, utils
HS = {0: (hashes.SHA256, hashlib.sha256), 1: (hashes.SHA384, hashlib.sha384), 2: (hashes.SHA512, hashlib.sha512)}
msg = bytes((i * 37 + 11) & 255 for i in range(200))
out = ["/* GENERE par tests/gen_pk_vectors.py : ne pas modifier a la main. */",
       "struct v_rsa { int pss, hashalg; const char *n, *e, *hash, *sig; };",
       "static const struct v_rsa V_RSA[] = {"]
for bits in (1024, 2048, 2048, 3072, 4096):
    k = rsa.generate_private_key(65537, bits)
    pn = k.public_key().public_numbers()
    n = pn.n.to_bytes((pn.n.bit_length() + 7) // 8, "big"); e = pn.e.to_bytes(3, "big")
    for pss in (0, 1):
        for ha, (HC, hf) in HS.items():
            if bits == 1024 and ha == 2 and pss: continue          # PSS SHA-512 ne tient pas dans 1024 bits
            if bits != 2048 and ha == 1: continue                  # on allege : les 3 hachages sur les cles 2048 seulement
            h = hf(msg).digest()
            if pss:
                sig = k.sign(msg, padding.PSS(padding.MGF1(HC()), HC().digest_size), HC())
            else:
                sig = k.sign(msg, padding.PKCS1v15(), HC())
            out.append('    { %d, %d, "%s", "%s", "%s", "%s" },' % (pss, ha, n.hex(), e.hex(), h.hex(), sig.hex()))
out.append("};")
# --- remplissages PKCS#1 mal formes, signes "a la main" avec la cle privee : doivent tous etre refuses ---
out += ["static const struct v_rsa V_RSA_BAD[] = {"]
k = rsa.generate_private_key(65537, 2048); pn = k.public_key().public_numbers(); pv = k.private_numbers()
n = pn.n.to_bytes(256, "big"); e = pn.e.to_bytes(3, "big")
h = hashlib.sha256(msg).digest()
DI = bytes.fromhex("3031300d060960864801650304020105000420")
def raw(em): return pow(int.from_bytes(em, "big"), pv.d, pn.n).to_bytes(256, "big")
good = b"\x00\x01" + b"\xff" * (256 - 3 - len(DI) - 32) + b"\x00" + DI + h
cases = [good,                                                                              # temoin : valide (le test l'attend)
         good[:2 + (256 - 3 - len(DI) - 32)] + b"\x01" + good[3 + (256 - 3 - len(DI) - 32):],  # separateur non nul
         b"\x00\x02" + good[2:],                                                              # type de bloc 2
         b"\x00\x01" + b"\xff" * 5 + b"\x00" + DI + h + b"\x00" * (256 - 3 - 5 - len(DI) - 32),  # remplissage court + queue
         b"\x00\x01" + b"\xff" * (256 - 3 - len(DI) - 32 - 1) + b"\x00" + DI + h + b"\x00",     # un octet de trop apres le condense
         b"\x01\x01" + good[2:],                                                              # premier octet non nul
         b"\x00\x01" + b"\xfe" + b"\xff" * (256 - 4 - len(DI) - 32) + b"\x00" + DI + h]         # un octet de remplissage faux
for i, em in enumerate(cases):
    assert len(em) == 256, (i, len(em))
    out.append('    { 0, 0, "%s", "%s", "%s", "%s" },' % (n.hex(), e.hex(), h.hex(), raw(em).hex()))
out.append("};")
out += ["struct v_ec { int curve; const char *pub, *hash, *sig; };", "static const struct v_ec V_EC[] = {"]
for cid, curve in ((1, ec.SECP256R1()), (2, ec.SECP384R1())):
    for _ in range(4):
        k = ec.generate_private_key(curve)
        pn = k.public_key().public_numbers(); L = (curve.key_size + 7) // 8
        pub = b"\x04" + pn.x.to_bytes(L, "big") + pn.y.to_bytes(L, "big")
        for ha, (HC, hf) in HS.items():
            sig = k.sign(msg, ec.ECDSA(HC()))
            out.append('    { %d, "%s", "%s", "%s" },' % (cid, pub.hex(), hf(msg).digest().hex(), sig.hex()))
# condense numeriquement >= ordre n : doit etre reduit modulo n (signature "prehashed")
for cid, curve, hc in ((1, ec.SECP256R1(), hashes.SHA256), (2, ec.SECP384R1(), hashes.SHA384)):
    for _ in range(3):
        k = ec.generate_private_key(curve)
        pn = k.public_key().public_numbers(); L = (curve.key_size + 7) // 8
        pub = b"\x04" + pn.x.to_bytes(L, "big") + pn.y.to_bytes(L, "big")
        dg = b"\xff" * L
        sig = k.sign(dg, ec.ECDSA(utils.Prehashed(hc())))
        out.append('    { %d, "%s", "%s", "%s" },' % (cid, pub.hex(), dg.hex(), sig.hex()))
out.append("};")
print("\n".join(out))
