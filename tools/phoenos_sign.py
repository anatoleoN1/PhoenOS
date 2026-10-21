#!/usr/bin/env python3
"""PhoenOS - outil de signature des mises a jour (0.10.4). Python 3 seul, AUCUNE dependance : tourne sous WSL/Windows.

  keygen  [--force]        cree la cle de signature (~/.phoenos/signing.key) et ecrit la cle PUBLIQUE dans src/updkey.h
  applykey                 ecrit dans src/updkey.h la cle publique de VOTRE cle existante (a faire apres chaque nouveau zip du projet,
                           car les zip contiennent une cle vide ; publier.sh le fait tout seul)
  pubkey                   affiche la cle publique et son empreinte
  release --edition E --version X.Y.Z --elf build/phoenos.bin --initrd build/isodir/boot/initrd.tar --out DIR [--notes TEXTE]
                           fabrique les 4 fichiers d'une edition : manifest-E.txt, manifest-E.txt.sig,
                           phoenos-E-X.Y.Z.kernel, phoenos-E-X.Y.Z.initrd
  verify  MANIFEST SIG     verifie une signature avec la cle publique de src/updkey.h

LA CLE PRIVEE NE QUITTE JAMAIS VOTRE ORDINATEUR : ne la mettez pas sur GitHub, ne l'envoyez a personne. Faites-en une
sauvegarde (cle USB). Si vous la perdez, les PhoenOS deja installes ne pourront plus se mettre a jour : il faudra
refaire une ISO avec une nouvelle cle.
"""
import argparse, hashlib, os, re, struct, sys

# ------------------------------------------------------------------ Ed25519 (RFC 8032, version de reference)
P = 2 ** 255 - 19
L = 2 ** 252 + 27742317777372353535851937790883648493
D = -121665 * pow(121666, P - 2, P) % P
I = pow(2, (P - 1) // 4, P)


def _inv(x):
    return pow(x, P - 2, P)


def _xrecover(y):
    xx = (y * y - 1) * _inv(D * y * y + 1)
    x = pow(xx, (P + 3) // 8, P)
    if (x * x - xx) % P != 0:
        x = (x * I) % P
    if x % 2 != 0:
        x = P - x
    return x


BY = 4 * _inv(5) % P
BX = _xrecover(BY)
B = (BX % P, BY % P, 1, (BX * BY) % P)


def _add(p, q):
    a = (p[1] - p[0]) * (q[1] - q[0]) % P
    b = (p[1] + p[0]) * (q[1] + q[0]) % P
    c = 2 * p[3] * q[3] * D % P
    d = 2 * p[2] * q[2] % P
    e, f, g, h = b - a, d - c, d + c, b + a
    return (e * f % P, g * h % P, f * g % P, e * h % P)


def _mul(s, p):
    q = (0, 1, 1, 0)
    while s > 0:
        if s & 1:
            q = _add(q, p)
        p = _add(p, p)
        s >>= 1
    return q


def _enc(p):
    zi = _inv(p[2])
    x, y = p[0] * zi % P, p[1] * zi % P
    return int.to_bytes(y | ((x & 1) << 255), 32, "little")


def _dec(s):
    y = int.from_bytes(s, "little")
    sign = y >> 255
    y &= (1 << 255) - 1
    x = _xrecover(y)
    if x & 1 != sign:
        x = P - x
    pt = (x, y, 1, x * y % P)
    return pt


def _h(m):
    return hashlib.sha512(m).digest()


def _clamp(seed):
    h = bytearray(_h(seed)[:32])
    h[0] &= 248
    h[31] &= 127
    h[31] |= 64
    return int.from_bytes(h, "little")


def public_key(seed):
    return _enc(_mul(_clamp(seed), B))


def sign(seed, msg):
    h = _h(seed)
    a = _clamp(seed)
    pk = _enc(_mul(a, B))
    r = int.from_bytes(_h(h[32:] + msg), "little") % L
    R = _enc(_mul(r, B))
    k = int.from_bytes(_h(R + pk + msg), "little") % L
    S = (r + k * a) % L
    return R + int.to_bytes(S, 32, "little")


def verify(pk, msg, sig):
    if len(sig) != 64 or len(pk) != 32:
        return False
    try:
        A = _dec(pk)
        R = _dec(sig[:32])
    except Exception:
        return False
    S = int.from_bytes(sig[32:], "little")
    if S >= L:
        return False
    k = int.from_bytes(_h(sig[:32] + pk + msg), "little") % L
    lhs = _mul(S, B)
    rhs = _add(R, _mul(k, A))
    return _enc(lhs) == _enc(rhs)


# ------------------------------------------------------------------ formats PhoenOS
EDITIONS = {"perf32": 1, "perf64": 2, "pui64": 3}
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KEYFILE = os.environ.get("PHOENOS_KEY", os.path.join(os.path.expanduser("~"), ".phoenos", "signing.key"))
UPDKEY_H = os.path.join(ROOT, "src", "updkey.h")


def build_number(version):
    m = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)", version)
    if not m:
        raise SystemExit("ERREUR : version invalide : %r (attendu X.Y.Z)" % version)
    x, y, z = (int(g) for g in m.groups())
    if x > 99 or y > 999 or z > 999:
        raise SystemExit("ERREUR : numero de version trop grand")
    return x * 1000000 + y * 1000 + z


def read_seed():
    try:
        txt = open(KEYFILE).read().strip()
    except OSError:
        raise SystemExit("ERREUR : cle de signature introuvable (%s). Lancez d'abord :  python3 tools/phoenos_sign.py keygen" % KEYFILE)
    if not re.fullmatch(r"[0-9a-f]{64}", txt):
        raise SystemExit("ERREUR : %s n'est pas une cle valide (64 chiffres hexadecimaux attendus)" % KEYFILE)
    return bytes.fromhex(txt)


def write_updkey(pk):
    arr = ", ".join("0x%02x" % b for b in pk)
    with open(UPDKEY_H, "w") as f:
        f.write("/* Cle PUBLIQUE de signature des mises a jour (generee par tools/phoenos_sign.py keygen). */\n"
                "#ifndef PHOENOS_UPDKEY_H\n#define PHOENOS_UPDKEY_H\n"
                "#define UPDATE_KEY_PRESENT 1\n"
                "static const unsigned char UPDATE_PUBKEY[32] = { %s };\n#endif\n" % arr)


def read_updkey():
    try:
        txt = open(UPDKEY_H).read()
    except OSError:
        return None
    if "UPDATE_KEY_PRESENT 1" not in txt:
        return None
    nums = re.findall(r"0x([0-9a-f]{2})", txt)
    return bytes(int(n, 16) for n in nums) if len(nums) == 32 else None


def cmd_keygen(a):
    if os.path.exists(KEYFILE) and not a.force:
        raise SystemExit("La cle existe deja : %s (--force pour la REMPLACER : les PhoenOS deja installes n'accepteraient plus vos mises a jour)." % KEYFILE)
    os.makedirs(os.path.dirname(KEYFILE), exist_ok=True)
    seed = os.urandom(32)
    fd = os.open(KEYFILE, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w") as f:
        f.write(seed.hex() + "\n")
    pk = public_key(seed)
    write_updkey(pk)
    print("Cle privee   : %s   (SECRETE : sauvegardez-la, ne la partagez jamais)" % KEYFILE)
    print("Cle publique : %s" % pk.hex())
    print("Ecrite dans  : %s   (a recompiler dans l'ISO : make iso)" % UPDKEY_H)
    print("Empreinte    : %s" % hashlib.sha256(pk).hexdigest()[:16])


def cmd_applykey(a):
    pk = public_key(read_seed())
    write_updkey(pk)
    print("src/updkey.h : cle publique %s... (empreinte %s)" % (pk.hex()[:16], hashlib.sha256(pk).hexdigest()[:16]))


def cmd_pubkey(a):
    pk = public_key(read_seed())
    print(pk.hex())
    print("empreinte", hashlib.sha256(pk).hexdigest()[:16])


def elf_to_image(elf, edition):
    """ELF 32 bits du noyau -> image PBI1 : 32 octets d'en-tete + octets a charger (le .bss est rempli de zeros au lancement)."""
    if elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        raise SystemExit("ERREUR : le noyau n'est pas un ELF 32 bits petit-boutiste")
    entry, phoff = struct.unpack_from("<II", elf, 24)[0], struct.unpack_from("<I", elf, 28)[0]
    phentsize, phnum = struct.unpack_from("<HH", elf, 42)
    segs = []
    for i in range(phnum):
        t, off, vaddr, paddr, filesz, memsz, flags, align = struct.unpack_from("<8I", elf, phoff + i * phentsize)
        if t == 1 and memsz:
            segs.append((paddr, off, filesz, memsz))
    if not segs:
        raise SystemExit("ERREUR : aucun segment a charger dans le noyau")
    lo = min(s[0] for s in segs)
    hi_file = max(s[0] + s[2] for s in segs)
    hi_mem = max(s[0] + s[3] for s in segs)
    payload = bytearray(hi_file - lo)
    for paddr, off, filesz, memsz in segs:
        payload[paddr - lo: paddr - lo + filesz] = elf[off: off + filesz]
    if not (lo <= entry < lo + len(payload)):
        raise SystemExit("ERREUR : point d'entree hors de l'image")
    while len(payload) % 4:
        payload.append(0)
    hdr = b"PBI1" + struct.pack("<7I", 1, lo, entry, len(payload), hi_mem - lo, EDITIONS[edition], 0)
    return hdr + bytes(payload)


def make_manifest(edition, version, kernel_name, kernel, initrd_name, initrd, notes):
    notes = " ".join(notes.split())[:80]
    lines = ["PHOENOS-MANIFEST 1",
             "edition " + edition,
             "version " + version,
             "build %d" % build_number(version),
             "kernel %s %d %s" % (kernel_name, len(kernel), hashlib.sha256(kernel).hexdigest()),
             "initrd %s %d %s" % (initrd_name, len(initrd), hashlib.sha256(initrd).hexdigest())]
    if notes:
        lines.append("notes " + notes)
    return ("\n".join(lines) + "\n").encode()


def cmd_release(a):
    if a.edition not in EDITIONS:
        raise SystemExit("ERREUR : edition inconnue %r (perf32, perf64, pui64)" % a.edition)
    seed = read_seed()
    pk = public_key(seed)
    embedded = read_updkey()
    if embedded != pk:
        raise SystemExit("ERREUR : la cle publique de src/updkey.h n'est pas celle de votre cle privee : l'ISO de cette version\n"
                         "        n'accepterait pas ces mises a jour. Lancez keygen (ou restaurez votre cle) puis recompilez.")
    elf = open(a.elf, "rb").read()
    initrd = open(a.initrd, "rb").read()
    kernel = elf_to_image(elf, a.edition)
    if len(kernel) > 2 * 1024 * 1024:
        raise SystemExit("ERREUR : noyau trop gros pour un emplacement de mise a jour (2 Mio)")
    if len(initrd) > 6273536:
        raise SystemExit("ERREUR : initrd trop gros pour un emplacement de mise a jour (6273536 octets)")
    kn, inn = "phoenos-%s-%s.kernel" % (a.edition, a.version), "phoenos-%s-%s.initrd" % (a.edition, a.version)
    man = make_manifest(a.edition, a.version, kn, kernel, inn, initrd, a.notes or "")
    if len(man) > 900:
        raise SystemExit("ERREUR : manifeste trop long")
    sig = sign(seed, man)
    assert verify(pk, man, sig)
    os.makedirs(a.out, exist_ok=True)
    for name, data in ((kn, kernel), (inn, initrd), ("manifest-%s.txt" % a.edition, man), ("manifest-%s.txt.sig" % a.edition, sig)):
        with open(os.path.join(a.out, name), "wb") as f:
            f.write(data)
    print("%s : %s %s (build %d) -> %s" % (a.edition, "PhoenOS", a.version, build_number(a.version), a.out))


def cmd_verify(a):
    pk = read_updkey()
    if pk is None:
        raise SystemExit("ERREUR : pas de cle publique dans src/updkey.h")
    ok = verify(pk, open(a.manifest, "rb").read(), open(a.sig, "rb").read())
    print("signature VALIDE" if ok else "signature INVALIDE")
    sys.exit(0 if ok else 1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("keygen"); s.add_argument("--force", action="store_true"); s.set_defaults(f=cmd_keygen)
    s = sub.add_parser("applykey"); s.set_defaults(f=cmd_applykey)
    s = sub.add_parser("pubkey"); s.set_defaults(f=cmd_pubkey)
    s = sub.add_parser("release")
    s.add_argument("--edition", required=True); s.add_argument("--version", required=True)
    s.add_argument("--elf", required=True); s.add_argument("--initrd", required=True)
    s.add_argument("--out", required=True); s.add_argument("--notes", default="")
    s.set_defaults(f=cmd_release)
    s = sub.add_parser("verify"); s.add_argument("manifest"); s.add_argument("sig"); s.set_defaults(f=cmd_verify)
    a = ap.parse_args()
    a.f(a)


if __name__ == "__main__":
    main()
