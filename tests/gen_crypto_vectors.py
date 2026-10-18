#!/usr/bin/env python3
"""Genere tests/crypto_vectors.h : vecteurs de reference calcules par la bibliotheque "cryptography" et hashlib
(donc independants du code C de PhoenOS). Deterministe : relancer donne le meme fichier.
Usage : python3 tests/gen_crypto_vectors.py > tests/crypto_vectors.h
Verifie aussi les constantes de la courbe 25519 ecrites dans src/c25519.c."""
import hashlib, hmac as hm, sys
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.kdf.hkdf import HKDFExpand, HKDF
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305, AESGCM
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives import serialization as S

def pat(n, salt):                       # octets reproductibles
    return bytes(((i * 131 + salt * 17 + (i >> 3) * 7 + 5) & 255) for i in range(n))
H = lambda b: b.hex()
out = []
def emit(s=""): out.append(s)

emit("/* GENERE par tests/gen_crypto_vectors.py : ne pas modifier a la main. */")
LENS = [0, 1, 3, 55, 56, 63, 64, 65, 111, 112, 119, 120, 127, 128, 129, 200, 1000, 4097]
emit("struct v_hash { u32 len; const char *msg_seed; const char *s256, *s384, *s512; };")
emit("static const struct v_hash V_HASH[] = {")
for n in LENS:
    m = pat(n, 1)
    emit('    { %d, "%s", "%s", "%s", "%s" },' % (n, H(m[:0]), hashlib.sha256(m).hexdigest(), hashlib.sha384(m).hexdigest(), hashlib.sha512(m).hexdigest()))
emit("};")

emit("struct v_hmac { int alg; u32 klen, mlen; const char *mac; };")
emit("static const struct v_hmac V_HMAC[] = {")
for alg, name in ((0, "sha256"), (1, "sha384")):
    for kl in (0, 1, 20, 63, 64, 65, 128, 131, 300):
        for ml in (0, 1, 50, 300):
            k, m = pat(kl, 2), pat(ml, 3)
            emit('    { %d, %d, %d, "%s" },' % (alg, kl, ml, hm.new(k, m, name).hexdigest()))
emit("};")

emit("struct v_hkdf { int alg; u32 ilen, slen, ilen_info, olen; const char *okm; };")
emit("static const struct v_hkdf V_HKDF[] = {")
for alg, h in ((0, hashes.SHA256()), (1, hashes.SHA384())):
    hl = h.digest_size
    for il, sl, inf, ol in ((22, 13, 10, 42), (80, 80, 80, 82), (22, 0, 0, 42), (32, 32, 0, 1), (16, 8, 5, hl), (16, 8, 5, hl + 1), (32, 0, 7, 255 * hl)):
        ikm, salt, info = pat(il, 4), pat(sl, 5), pat(inf, 6)
        okm = HKDF(algorithm=h, length=ol, salt=salt if sl else None, info=info).derive(ikm)
        emit('    { %d, %d, %d, %d, %d, "%s" },' % (alg, il, sl, inf, ol, H(okm)))
emit("};")

emit("struct v_aead { u32 kseed, klen, alen, mlen; const char *ct_tag; };")
emit("static const struct v_aead V_CHACHA[] = {")
for ks in (7, 31, 32, 33, 34, 35, 36, 37):
    key = pat(32, ks); nonce = pat(12, ks + 1)
    for al in (0, 1, 16, 33):
        for ml in ((0, 1, 16, 17, 64, 65, 114, 1000) if ks == 7 else (1, 17, 130)):
            emit('    { %d, 32, %d, %d, "%s" },' % (ks, al, ml, H(ChaCha20Poly1305(key).encrypt(nonce, pat(ml, 9), pat(al, 10)))))
emit("};")
emit("static const struct v_aead V_GCM[] = {")
for ks in (11, 41, 42, 43):
    nonce = pat(12, ks + 1)
    for kl in (16, 32):
        k = pat(kl, ks)
        for al in (0, 13, 16, 33):
            for ml in ((0, 1, 15, 16, 17, 63, 64, 65, 114, 1000) if ks == 11 else (1, 17)):
                emit('    { %d, %d, %d, %d, "%s" },' % (ks, kl, al, ml, H(AESGCM(k).encrypt(nonce, pat(ml, 12), pat(al, 13)))))
emit("};")

from cryptography.hazmat.primitives.poly1305 import Poly1305
emit("struct v_poly { const char *key, *msg, *tag; };")
emit("static const struct v_poly V_POLY[] = {")
FF = b"\xff"
def le(n): return n.to_bytes(16, "little")
polycases = []
for r, sk, msg in (
    (2, 0, FF*16), (2, (1<<128)-1, le(2)), (1, 0, FF*16 + b"\xf0" + FF*15 + le(0x11)),
    (1, 0, FF*16 + b"\xfb" + b"\xfe"*15 + b"\x01"*16), (2, 0, b"\xfd" + FF*15),
    (1, 0, b"\xe3\x35\x94\xd7\x50\x5e\x43\xb9" + b"\x00"*8 + b"\x33\x94\xd7\x50\x5e\x43\x79\xcd\x01" + b"\x00"*7),
    (1, 0, b"\xff"*17), (5, 3, b"\xff"*33), (0x0ffffffc0ffffffc0ffffffc0fffffff, 0x1234, pat(100, 15)),
    (0x0fffffff0ffffffc0ffffffc0ffffffc, (1<<127)+99, pat(1, 16)), (0x0ffffffc0ffffffc0ffffffc0ffffffc, 0, b"")):
    k = le(r) + le(sk)
    polycases.append((k, msg))
for k, msg in polycases:
    emit('    { "%s", "%s", "%s" },' % (H(k), H(msg), H(Poly1305.generate_tag(k, msg))))
emit("};")

emit("struct v_x25519 { const char *scalar, *point, *out; };")
emit("static const struct v_x25519 V_X25519[] = {")
for i in range(12):
    sk = pat(32, 20 + i)
    if i < 6:
        pt = pat(32, 40 + i)
    else:                                # points valides : cle publique d'un autre secret
        pt = X25519PrivateKey.from_private_bytes(pat(32, 60 + i)).public_key().public_bytes(S.Encoding.Raw, S.PublicFormat.Raw)
    try:
        r = X25519PrivateKey.from_private_bytes(sk).exchange(X25519PublicKey.from_public_bytes(pt))
    except ValueError:
        continue
    emit('    { "%s", "%s", "%s" },' % (H(sk), H(pt), H(r)))
emit("};")
emit("struct v_x25519b { const char *scalar, *pub; };")
emit("static const struct v_x25519b V_X25519B[] = {")
for i in range(6):
    sk = pat(32, 80 + i)
    emit('    { "%s", "%s" },' % (H(sk), H(X25519PrivateKey.from_private_bytes(sk).public_key().public_bytes(S.Encoding.Raw, S.PublicFormat.Raw))))
emit("};")

emit("struct v_ed { const char *seed, *pk; u32 mlen; const char *sig; };")
emit("static const struct v_ed V_ED[] = {")
for i, ml in enumerate((0, 1, 2, 3, 64, 100, 129, 1000)):
    sd = pat(32, 100 + i); sk = Ed25519PrivateKey.from_private_bytes(sd)
    pk = sk.public_key().public_bytes(S.Encoding.Raw, S.PublicFormat.Raw)
    emit('    { "%s", "%s", %d, "%s" },' % (H(sd), H(pk), ml, H(sk.sign(pat(ml, 14)))))
emit("};")
emit("/* le message de V_HASH/V_HMAC/... est pat(n, salt) : generateur identique cote C (voir test_crypto.c) */")

# --- verification des constantes de src/c25519.c ---
p = 2**255 - 19
src = open("src/c25519.c").read()
def limbs(name):
    import re
    m = re.search(r"static const gf %s = \{([^}]*)\}" % name, src)
    v = 0
    for i, x in enumerate(m.group(1).split(",")):
        v |= int(x, 16) << (16 * i)
    return v
d = (-121665 * pow(121666, p - 2, p)) % p
assert limbs("D") == d and limbs("D2") == 2 * d % p
assert limbs("GY") == 4 * pow(5, p - 2, p) % p
assert limbs("GI") ** 2 % p == p - 1
x = limbs("GX"); y = limbs("GY")
assert (-x * x + y * y - 1 - d * x * x * y * y) % p == 0 and x % 2 == 0
sys.stderr.write("constantes c25519 verifiees\n")
print("\n".join(out))
