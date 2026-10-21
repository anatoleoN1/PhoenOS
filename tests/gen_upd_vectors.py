#!/usr/bin/env python3
"""Genere tests/upd_vectors.h et tests/test_updkey.h : cle de TEST (jamais utilisee ailleurs), manifestes signes, images factices.
Aucune dependance (utilise tools/phoenos_sign.py). Relancer pour regenerer : python3 tests/gen_upd_vectors.py"""
import hashlib, os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools"))
import phoenos_sign as S

SEED = hashlib.sha256(b"phoenos-test-signing-key").digest()
OTHER = hashlib.sha256(b"phoenos-another-key").digest()
PK = S.public_key(SEED)


def prng(tag, n):
    out = b""
    i = 0
    while len(out) < n:
        out += hashlib.sha256(tag + struct.pack("<I", i)).digest()
        i += 1
    return out[:n]


def image(edition_id, payload_len=4096, memsz=20480, load=0x100000, entry_off=16, tag=b"k"):
    payload = prng(tag, payload_len)
    hdr = b"PBI1" + struct.pack("<7I", 1, load, load + entry_off, payload_len, memsz, edition_id, 0)
    return hdr + payload


def arr(name, data):
    lines = []
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join("0x%02x" % b for b in data[i:i + 16]) + ",")
    if not data:
        lines = ["    0,"]
    return "static const unsigned char %s[] = {\n%s\n};\nstatic const unsigned int %s_LEN = %d;\n" % (name, "\n".join(lines), name, len(data))


def man(edition, version, k, i, kname=None, notes="essai"):
    return S.make_manifest(edition, version, kname or "phoenos-%s-%s.kernel" % (edition, version), k,
                           "phoenos-%s-%s.initrd" % (edition, version), i, notes)


K = image(1, tag=b"kernel")
I = prng(b"initrd", 20000)
K_X64 = image(2, tag=b"kernel64")                    # image d'une autre edition (en-tete perf64)
K_SMALL = image(1, payload_len=2048, memsz=8192, tag=b"kernel2")

items = []
items.append(arr("V_PUBKEY", PK))
items.append(arr("V_KERNEL", K))
items.append(arr("V_INITRD", I))
items.append(arr("V_KERNEL_X64", K_X64))
M = man("perf32", "0.10.5", K, I)
items.append(arr("V_MAN", M)); items.append(arr("V_SIG", S.sign(SEED, M)))
M2 = man("perf32", "0.10.5", K, I, notes="")
items.append(arr("V_MAN_NONOTES", M2)); items.append(arr("V_SIG_NONOTES", S.sign(SEED, M2)))
M = man("perf32", "0.10.5", K, I)
items.append(arr("V_SIG_OTHER", S.sign(OTHER, M)))                       # meme manifeste, autre cle
M64 = man("perf64", "0.10.5", K_X64, I)
items.append(arr("V_MAN_X64", M64)); items.append(arr("V_SIG_X64", S.sign(SEED, M64)))
MOLD = man("perf32", "0.10.2", K, I)
items.append(arr("V_MAN_OLD", MOLD)); items.append(arr("V_SIG_OLD", S.sign(SEED, MOLD)))
MSAME = man("perf32", "0.10.4", K, I)
items.append(arr("V_MAN_SAME", MSAME)); items.append(arr("V_SIG_SAME", S.sign(SEED, MSAME)))
MNEW = man("perf32", "0.10.6", K_SMALL, I)
items.append(arr("V_KERNEL_NEW", K_SMALL))
items.append(arr("V_MAN_NEW", MNEW)); items.append(arr("V_SIG_NEW", S.sign(SEED, MNEW)))
# manifeste signe dont le noyau est d'une autre edition (hash correct, en-tete perf64) : l'image doit etre refusee
MBI = man("perf32", "0.10.7", K_X64, I)
items.append(arr("V_MAN_BADIMG", MBI)); items.append(arr("V_SIG_BADIMG", S.sign(SEED, MBI)))

with open(os.path.join(HERE, "upd_vectors.h"), "w") as f:
    f.write("/* Genere par tests/gen_upd_vectors.py : NE PAS EDITER A LA MAIN. */\n#ifndef UPD_VECTORS_H\n#define UPD_VECTORS_H\n")
    f.write("\n".join(items))
    f.write("#endif\n")
with open(os.path.join(HERE, "test_updkey.h"), "w") as f:
    f.write("/* Cle publique de TEST (tests/gen_upd_vectors.py) : remplace src/updkey.h dans les tests seulement. */\n"
            "#ifndef PHOENOS_UPDKEY_H\n#define PHOENOS_UPDKEY_H\n#define UPDATE_KEY_PRESENT 1\n"
            "static const unsigned char UPDATE_PUBKEY[32] = { %s };\n#endif\n" % ", ".join("0x%02x" % b for b in PK))
print("ok :", len(K), "octets de noyau,", len(I), "d'initrd")
