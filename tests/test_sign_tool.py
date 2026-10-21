#!/usr/bin/env python3
"""Teste tools/phoenos_sign.py : vecteurs RFC 8032, recoupement avec `cryptography` (si present), numero de build,
conversion ELF -> image PBI1 (identique a objcopy), `release` + `verify` de bout en bout, refus d'une mauvaise cle."""
import os, re, subprocess, sys, tempfile, shutil
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import phoenos_sign as S
fails = 0
def ck(c, m):
    global fails
    if not c: print("  ECHEC :", m); fails += 1

# RFC 8032, section 7.1, vecteurs 1 et 2
v = [("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60", "",
      "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
      "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"),
     ("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb", "72",
      "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
      "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00")]
for seed, msg, pk, sig in v:
    seed, msg = bytes.fromhex(seed), bytes.fromhex(msg)
    ck(S.public_key(seed).hex() == pk, "cle publique RFC")
    ck(S.sign(seed, msg).hex() == sig, "signature RFC")
    ck(S.verify(bytes.fromhex(pk), msg, bytes.fromhex(sig)), "verification RFC")
    bad = bytearray(bytes.fromhex(sig)); bad[10] ^= 1
    ck(not S.verify(bytes.fromhex(pk), msg, bytes(bad)), "signature alteree refusee")
try:
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    for i in range(20):
        seed = os.urandom(32); msg = os.urandom(i * 7)
        k = Ed25519PrivateKey.from_private_bytes(seed)
        ck(k.sign(msg) == S.sign(seed, msg), "recoupement cryptography")
except ImportError:
    print("  (cryptography absent : recoupement saute)")

# numero de build
ver = re.search(r'PHOENOS_VERSION\s+"([^"]+)"', open(os.path.join(ROOT, "src/version.h")).read()).group(1)
bld = int(re.search(r'PHOENOS_BUILD\s+(\d+)u', open(os.path.join(ROOT, "src/version.h")).read()).group(1))
ck(S.build_number(ver) == bld, "PHOENOS_BUILD (%d) ne correspond pas a PHOENOS_VERSION (%s)" % (bld, ver))
ck(S.build_number("0.10.5") > S.build_number("0.10.4") > S.build_number("0.9.9"), "ordre des builds")
ck(S.build_number("1.0.0") > S.build_number("0.100.0"), "1.0.0 apres 0.100.0")

# ELF -> image, release, verify (si un noyau est construit)
elf = os.path.join(ROOT, "build/phoenos.bin"); ird = os.path.join(ROOT, "build/isodir/boot/initrd.tar")
if os.path.exists(elf) and os.path.exists(ird):
    img = S.elf_to_image(open(elf, "rb").read(), "perf32")
    ck(img[:4] == b"PBI1", "en-tete PBI1")
    with tempfile.TemporaryDirectory() as t:
        shutil.copytree(os.path.join(ROOT, "tools"), t + "/tools"); os.makedirs(t + "/src")
        shutil.copy(os.path.join(ROOT, "src/updkey.h"), t + "/src/updkey.h")
        env = dict(os.environ, HOME=t)
        run = lambda *a: subprocess.run([sys.executable, t + "/tools/phoenos_sign.py", *a], env=env, cwd=t, capture_output=True, text=True)
        r = run("keygen"); ck(r.returncode == 0, "keygen : " + r.stderr)
        r = run("release", "--edition", "perf32", "--version", ver, "--elf", elf, "--initrd", ird, "--out", t + "/out")
        ck(r.returncode == 0, "release : " + r.stdout + r.stderr)
        r = run("verify", t + "/out/manifest-perf32.txt", t + "/out/manifest-perf32.txt.sig")
        ck(r.returncode == 0 and "VALIDE" in r.stdout, "verify")
        k = open(t + "/out/phoenos-perf32-%s.kernel" % ver, "rb").read()
        ck(k == img, "noyau publie = image calculee")
        open(t + "/src/updkey.h", "w").write(open(os.path.join(ROOT, "src/updkey.h")).read())
        r = run("applykey"); ck(r.returncode == 0, "applykey")
        r = run("verify", t + "/out/manifest-perf32.txt", t + "/out/manifest-perf32.txt.sig")
        ck(r.returncode == 0, "apres applykey : la signature d'origine est valide")
        r = run("keygen", "--force")                    # nouvelle cle : l'ancienne signature ne doit plus etre acceptee
        r = run("verify", t + "/out/manifest-perf32.txt", t + "/out/manifest-perf32.txt.sig")
        ck(r.returncode != 0, "autre cle : signature refusee")
        r = run("release", "--edition", "perf32", "--version", ver, "--elf", elf, "--initrd", ird, "--out", t + "/out2")
        ck(r.returncode == 0, "release apres changement de cle (cle et updkey.h concordent)")
        open(t + "/src/updkey.h", "w").write(open(os.path.join(ROOT, "src/updkey.h")).read())
        r = run("release", "--edition", "perf32", "--version", ver, "--elf", elf, "--initrd", ird, "--out", t + "/out3")
        ck(r.returncode != 0, "release refuse si updkey.h ne correspond pas a la cle privee")
else:
    print("  (noyau non construit : ELF/release sautes)")
print("  outil de signature verifie" if not fails else "  => %d echec(s)" % fails)
sys.exit(1 if fails else 0)
