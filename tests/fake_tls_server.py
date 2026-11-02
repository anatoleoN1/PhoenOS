#!/usr/bin/env python3
"""Mini serveur TLS 1.3 (AES-128-GCM, X25519, ECDSA P-256) qui accepte UNE connexion, avec une faute injectee au choix.
Sert a verifier que le client PhoenOS refuse les serveurs qui trichent (OpenSSL ne sait pas faire ca).
Usage : fake_tls_server.py PORT FAUTE cert.pem key.pem [chaine.pem]
FAUTE : ok | bad_cv | bad_finished | zero_share | cv_other_key | hrr | bad_sid | no_version | empty_certs | extra_ext |
        cv_wrong_scheme | truncated_finished
Ecrit sur la sortie : RESULT client_finished_ok | RESULT client_closed (le client a rompu avant de finir)."""
import sys, socket, struct, hmac, hashlib, os
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives import hashes, serialization as S
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
port, fault, certpem, keypem = int(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4]
chain = sys.argv[5] if len(sys.argv) > 5 else None
def pem_to_der(path):
    import base64, re
    return [base64.b64decode(b) for b in re.findall(rb"-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----", open(path, "rb").read(), re.S)]
certs = pem_to_der(certpem) + (pem_to_der(chain) if chain else [])
key = S.load_pem_private_key(open(keypem, "rb").read(), None)
other = ec.generate_private_key(ec.SECP256R1())
H = hashlib.sha256
def hkdf_extract(salt, ikm): return hmac.new(salt, ikm, H).digest()
def expand_label(secret, label, ctx, n):
    info = struct.pack(">HB", n, 6 + len(label)) + b"tls13 " + label.encode() + bytes([len(ctx)]) + ctx
    out, t, i = b"", b"", 1
    while len(out) < n:
        t = hmac.new(secret, t + info + bytes([i]), H).digest(); out += t; i += 1
    return out[:n]
def derive(secret, label, th): return expand_label(secret, label, th, 32)

s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); s.bind(("127.0.0.1", port)); s.listen(1)
c, _ = s.accept(); c.settimeout(8)
def rd(n):
    b = b""
    while len(b) < n:
        d = c.recv(n - len(b))
        if not d: raise EOFError
        b += d
    return b
def rrec():
    h = rd(5); return h[0], rd(struct.unpack(">H", h[3:5])[0])
def wrec(t, body): c.sendall(bytes([t, 3, 3]) + struct.pack(">H", len(body)) + body)
transcript = hashlib.sha256()
try:
    t, ch = rrec(); assert t == 22 and ch[0] == 1
    transcript.update(ch)
    p = 4 + 2 + 32; sidlen = ch[p]; sid = ch[p + 1:p + 1 + sidlen]; p += 1 + sidlen
    cs = struct.unpack(">H", ch[p:p + 2])[0]; p += 2 + cs; p += 1 + ch[p]
    el = struct.unpack(">H", ch[p:p + 2])[0]; p += 2; end = p + el; cpub = None
    while p < end:
        et, l = struct.unpack(">HH", ch[p:p + 4]); p += 4
        if et == 0x33:
            q = p + 2
            while q < p + l:
                g, kl = struct.unpack(">HH", ch[q:q + 4])
                if g == 0x1d: cpub = ch[q + 4:q + 4 + kl]
                q += 4 + kl
        p += l
    sk = X25519PrivateKey.generate()
    spub = sk.public_key().public_bytes(S.Encoding.Raw, S.PublicFormat.Raw)
    if fault == "zero_share": spub = bytes(32)
    shared = sk.exchange(X25519PublicKey.from_public_bytes(cpub)) if fault != "zero_share" else bytes(32)
    exts = struct.pack(">HHH", 0x2b, 2, 0x0304) if fault != "no_version" else b""
    exts += struct.pack(">HHHH", 0x33, 36, 0x1d, 32) + spub
    if fault == "extra_ext": exts += struct.pack(">HHH", 0x0017, 2, 0x1234)
    rnd = bytes.fromhex("CF21AD74E59A6111BE1D8C021E65B891C2A211167ABB8C5E079E09E2C8A8339C") if fault == "hrr" else os.urandom(32)
    echo_sid = sid if fault != "bad_sid" else bytes(32)
    body = b"\x03\x03" + rnd + bytes([len(echo_sid)]) + echo_sid + b"\x13\x01" + b"\x00" + struct.pack(">H", len(exts)) + exts
    sh = b"\x02" + struct.pack(">I", len(body))[1:] + body
    transcript.update(sh); wrec(22, sh)
    wrec(20, b"\x01")                                   # CCS de compatibilite
    early = hkdf_extract(bytes(32), bytes(32))
    derived = derive(early, "derived", H(b"").digest())
    hs = hkdf_extract(derived, shared)
    ths = transcript.digest()
    c_hs, s_hs = derive(hs, "c hs traffic", ths), derive(hs, "s hs traffic", ths)
    master = hkdf_extract(derive(hs, "derived", H(b"").digest()), bytes(32))
    skey, siv = expand_label(s_hs, "key", b"", 16), expand_label(s_hs, "iv", b"", 12)
    seq = [0]
    def wenc(inner_type, data):
        ct_len = len(data) + 1 + 16
        hdr = bytes([23, 3, 3]) + struct.pack(">H", ct_len)
        nonce = bytes(a ^ b for a, b in zip(siv, bytes(4) + struct.pack(">Q", seq[0]))); seq[0] += 1
        c.sendall(hdr + AESGCM(skey).encrypt(nonce, data + bytes([inner_type]), hdr))
    def msg(t, body): return bytes([t]) + struct.pack(">I", len(body))[1:] + body
    def send(m): transcript.update(m); wenc(22, m)
    send(msg(8, b"\x00\x00"))
    if fault == "empty_certs":
        send(msg(11, b"\x00\x00\x00\x00"))
    else:
        cl = b"".join(struct.pack(">I", len(d))[1:] + d + b"\x00\x00" for d in certs)
        send(msg(11, b"\x00" + struct.pack(">I", len(cl))[1:] + cl))
    th = transcript.digest()
    if fault == "bad_cv": th = bytes(32)
    content = b"\x20" * 64 + b"TLS 1.3, server CertificateVerify\x00" + th
    signer = other if fault == "cv_other_key" else key
    sig = signer.sign(content, ec.ECDSA(hashes.SHA256()))
    scheme = 0x0403 if fault != "cv_wrong_scheme" else 0x0804
    send(msg(15, struct.pack(">HH", scheme, len(sig)) + sig))
    fk = expand_label(s_hs, "finished", b"", 32)
    vd = hmac.new(fk, transcript.digest(), H).digest()
    if fault == "bad_finished": vd = bytes([vd[0] ^ 1]) + vd[1:]
    if fault == "truncated_finished": vd = vd[:31]
    send(msg(20, vd))
    c_ap = derive(master, "c ap traffic", transcript.digest())
    # lire la fin du client : CCS puis Finished chiffre
    ckey, civ = expand_label(c_hs, "key", b"", 16), expand_label(c_hs, "iv", b"", 12)
    cseq = 0; ok = False
    while True:
        t, body = rrec()
        if t == 20: continue
        if t == 21: break
        hdr = bytes([t, 3, 3]) + struct.pack(">H", len(body))
        nonce = bytes(a ^ b for a, b in zip(civ, bytes(4) + struct.pack(">Q", cseq))); cseq += 1
        pt = AESGCM(ckey).decrypt(nonce, body, hdr).rstrip(b"\x00")
        if pt[-1] == 22 and pt[0] == 20:
            exp = hmac.new(expand_label(c_hs, "finished", b"", 32), transcript.digest(), H).digest()
            ok = pt[4:-1] == exp
            break
    print("RESULT client_finished_ok" if ok else "RESULT client_bad_finished")
except Exception as e:
    print("RESULT client_closed", type(e).__name__)
