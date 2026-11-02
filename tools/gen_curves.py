#!/usr/bin/env python3
"""Genere src/curves.inc (parametres des courbes P-256 et P-384) a partir de OpenSSL, sans rien taper a la main.
Usage : python3 tools/gen_curves.py > src/curves.inc"""
import subprocess, re
def params(name):
    t = subprocess.check_output(["openssl", "ecparam", "-param_enc", "explicit", "-text", "-noout", "-name", name]).decode()
    def block(label):
        m = re.search(label + r":?\s*((?:\s*[0-9a-f:]+\s*)+?)(?=\n[A-Za-z]|\Z)", t)
        return bytes.fromhex(re.sub(r"[^0-9a-f]", "", m.group(1)))
    g = block(r"Generator \(uncompressed\)")
    assert g[0] == 4
    p = int.from_bytes(block("Prime"), "big"); b = int.from_bytes(block("B"), "big"); n = int.from_bytes(block("Order"), "big")
    a = int.from_bytes(block("A"), "big"); assert a == p - 3
    L = (p.bit_length() + 7) // 8
    gx = int.from_bytes(g[1:1+L], "big"); gy = int.from_bytes(g[1+L:], "big")
    assert (gy*gy - gx*gx*gx - a*gx - b) % p == 0
    return L, p, n, b, gx, gy
def arr(name, v, L):
    bs = v.to_bytes(L, "big")
    return "static const u8 %s[%d] = {%s};" % (name, L, ", ".join("0x%02x" % x for x in bs))
print("/* GENERE par tools/gen_curves.py (OpenSSL) : ne pas modifier a la main. */")
for tag, name in (("P256", "prime256v1"), ("P384", "secp384r1")):
    L, p, n, b, gx, gy = params(name)
    for k, v in (("P", p), ("N", n), ("B", b), ("GX", gx), ("GY", gy)):
        print(arr("%s_%s" % (tag, k), v, L))
