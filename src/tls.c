/* Client TLS 1.3 (RFC 8446). Voir tls.h. */
#include "tls.h"
#include "x509.h"
#include "pubkey.h"
#include "rng.h"
#include "string.h"

#define HT_CLIENT_HELLO 1
#define HT_SERVER_HELLO 2
#define HT_NEW_SESSION_TICKET 4
#define HT_ENCRYPTED_EXTENSIONS 8
#define HT_CERTIFICATE 11
#define HT_CERTIFICATE_REQUEST 13
#define HT_CERTIFICATE_VERIFY 15
#define HT_FINISHED 20
#define HT_KEY_UPDATE 24

#define CT_CCS 20
#define CT_ALERT 21
#define CT_HANDSHAKE 22
#define CT_APPDATA 23

static const u8 HRR_RANDOM[32] = {0xCF,0x21,0xAD,0x74,0xE5,0x9A,0x61,0x11,0xBE,0x1D,0x8C,0x02,0x1E,0x65,0xB8,0x91,
                                  0xC2,0xA2,0x11,0x16,0x7A,0xBB,0x8C,0x5E,0x07,0x9E,0x09,0xE2,0xC8,0xA8,0x33,0x9C};

/* ------------------------------------------------------------------ */
/* Transport                                                           */
/* ------------------------------------------------------------------ */
static int io_send(struct tls *t, const u8 *d, u32 n) { return t->io.send(t->io.ctx, d, n) < 0 ? TLS_E_IO : TLS_OK; }

static int io_read_exact(struct tls *t, u8 *buf, u32 n)
{
    while (n) {
        int r = t->io.recv(t->io.ctx, buf, n);
        if (r <= 0)
            return TLS_E_IO;
        buf += r; n -= (u32)r;
    }
    return TLS_OK;
}

/* ------------------------------------------------------------------ */
/* Cles                                                                */
/* ------------------------------------------------------------------ */
static void expand_label(int alg, const u8 *secret, const char *label, const u8 *ctx, u32 ctxlen, u8 *out, u32 olen)
{
    u8 info[2 + 1 + 6 + 16 + 1 + HASH_MAX];
    u32 n = 0, ll = (u32)strlen(label);
    info[n++] = (u8)(olen >> 8); info[n++] = (u8)olen;
    info[n++] = (u8)(6 + ll);
    memcpy(info + n, "tls13 ", 6); n += 6;
    memcpy(info + n, label, ll); n += ll;
    info[n++] = (u8)ctxlen;
    if (ctxlen) { memcpy(info + n, ctx, ctxlen); n += ctxlen; }
    hkdf_expand(alg, secret, info, n, out, olen);
}

static void th_hash(const struct tls *t, u8 *out)
{
    hash_ctx c = t->th;
    hash_final(&c, out);
}

static void derive_secret(const struct tls *t, const u8 *secret, const char *label, u8 *out)
{
    u8 h[HASH_MAX];
    th_hash(t, h);
    expand_label(t->hash, secret, label, h, (u32)t->hashlen, out, (u32)t->hashlen);
}

static void dir_set_secret(struct tls *t, struct tls_dir *d, const u8 *secret)
{
    memcpy(d->secret, secret, (u32)t->hashlen);
    expand_label(t->hash, secret, "key", NULL, 0, d->key, (u32)t->keylen);
    expand_label(t->hash, secret, "iv", NULL, 0, d->iv, 12);
    d->seq_hi = d->seq_lo = 0;
    d->encrypted = 1;
    if (t->suite != 0x1303)
        aesgcm_init(&d->gcm, d->key, (u32)t->keylen);
}

static void make_nonce(const struct tls_dir *d, u8 nonce[12])
{
    memcpy(nonce, d->iv, 12);
    for (int i = 0; i < 4; i++) {
        nonce[4 + i] ^= (u8)(d->seq_hi >> (24 - 8 * i));
        nonce[8 + i] ^= (u8)(d->seq_lo >> (24 - 8 * i));
    }
}

static void seq_inc(struct tls_dir *d)
{
    if (++d->seq_lo == 0)
        d->seq_hi++;
}

/* ------------------------------------------------------------------ */
/* Enregistrements                                                     */
/* ------------------------------------------------------------------ */
static int rec_write(struct tls *t, u8 type, const u8 *data, u32 len)
{
    u8 nonce[12];
    u8 *o = t->out;
    if (len > TLS_REC_MAX)
        return TLS_E_PROTOCOL;
    if (!t->tx.encrypted) {
        o[0] = type; o[1] = 3; o[2] = 3; o[3] = (u8)(len >> 8); o[4] = (u8)len;
        memcpy(o + 5, data, len);
        return io_send(t, o, 5 + len);
    }
    {
        u32 total = len + 1, clen = total + 16;
        o[0] = CT_APPDATA; o[1] = 3; o[2] = 3; o[3] = (u8)(clen >> 8); o[4] = (u8)clen;
        memcpy(o + 5, data, len);
        o[5 + len] = type;
        make_nonce(&t->tx, nonce);
        if (t->suite == 0x1303)
            chacha20poly1305_seal(t->tx.key, nonce, o, 5, o + 5, total, o + 5, o + 5 + total);
        else
            aesgcm_seal(&t->tx.gcm, nonce, o, 5, o + 5, total, o + 5, o + 5 + total);
        seq_inc(&t->tx);
        return io_send(t, o, 5 + clen);
    }
}

/* Lit un enregistrement. Rend le type interne et un pointeur vers les donnees (dans t->rec). */
static int rec_read(struct tls *t, u8 *type, u8 **data, u32 *len)
{
    for (;;) {
        u32 n;
        int r = io_read_exact(t, t->rec, 5);
        if (r) return r;
        n = (u32)t->rec[3] << 8 | t->rec[4];
        if (t->rec[1] != 3 || n > TLS_REC_MAX + 256) return TLS_E_PROTOCOL;
        r = io_read_exact(t, t->rec + 5, n);
        if (r) return r;
        if (t->rec[0] == CT_CCS) {                               /* compatibilite : ignore */
            if (n != 1 || t->rec[5] != 1) return TLS_E_PROTOCOL;
            continue;
        }
        if (t->rx.encrypted) {
            u8 nonce[12];
            u32 ml;
            if (t->rec[0] != CT_APPDATA || n < 17) return TLS_E_PROTOCOL;
            ml = n - 16;
            make_nonce(&t->rx, nonce);
            if (t->suite == 0x1303)
                r = chacha20poly1305_open(t->rx.key, nonce, t->rec, 5, t->rec + 5, ml, t->rec + 5, t->rec + 5 + ml);
            else
                r = aesgcm_open(&t->rx.gcm, nonce, t->rec, 5, t->rec + 5, ml, t->rec + 5, t->rec + 5 + ml);
            if (r) return TLS_E_DECRYPT;
            seq_inc(&t->rx);
            while (ml && t->rec[5 + ml - 1] == 0) ml--;          /* remplissage */
            if (!ml) return TLS_E_PROTOCOL;
            *type = t->rec[5 + ml - 1];
            *len = ml - 1;
            if (*len > TLS_REC_MAX) return TLS_E_PROTOCOL;
            *data = t->rec + 5;
            return TLS_OK;
        }
        if (n > TLS_REC_MAX) return TLS_E_PROTOCOL;
        *type = t->rec[0];
        *len = n;
        *data = t->rec + 5;
        return TLS_OK;
    }
}

static void send_alert(struct tls *t, u8 desc)
{
    u8 a[2] = {2, desc};
    (void)rec_write(t, CT_ALERT, a, 2);
}

static int fail(struct tls *t, int err, u8 alert)
{
    if (!t->failed) {
        t->failed = 1;
        if (alert) send_alert(t, alert);
    }
    return err;
}

/* ------------------------------------------------------------------ */
/* Messages de poignee de main                                         */
/* ------------------------------------------------------------------ */
static void th_add(struct tls *t, const u8 *msg, u32 len) { hash_update(&t->th, msg, len); }

/* Attend un message complet dans t->hs. Renvoie son type, son contenu (body, blen) ; la taille avec l en-tete est blen + 4. */
static int hs_next(struct tls *t, u8 *mtype, const u8 **body, u32 *blen)
{
    for (;;) {
        if (t->hs_len >= 4) {
            u32 l = (u32)t->hs[1] << 16 | (u32)t->hs[2] << 8 | t->hs[3];
            if (l > TLS_HS_MAX - 4) return TLS_E_TOOBIG;
            if (t->hs_len >= 4 + l) {
                *mtype = t->hs[0]; *body = t->hs + 4; *blen = l;
                return TLS_OK;
            }
        }
        {
            u8 type;
            u8 *d;
            u32 n;
            int r = rec_read(t, &type, &d, &n);
            if (r) return r;
            if (type == CT_ALERT) {
                if (n >= 2) t->alert = d[1];
                return TLS_E_ALERT;
            }
            if (type != CT_HANDSHAKE) return TLS_E_PROTOCOL;
            if (t->hs_len + n > TLS_HS_MAX) return TLS_E_TOOBIG;
            memcpy(t->hs + t->hs_len, d, n);
            t->hs_len += n;
        }
    }
}

static void hs_pop(struct tls *t)
{
    u32 l = 4 + ((u32)t->hs[1] << 16 | (u32)t->hs[2] << 8 | t->hs[3]);
    memmove(t->hs, t->hs + l, t->hs_len - l);
    t->hs_len -= l;
}

static int is_ipv4(const char *s)
{
    int dots = 0;
    if (!*s) return 0;
    for (; *s; s++) {
        if (*s == '.') dots++;
        else if (*s < '0' || *s > '9') return 0;
    }
    return dots == 3;
}

struct wr { u8 *p; u32 n, cap; };
static void w8(struct wr *w, u32 v) { if (w->n < w->cap) w->p[w->n++] = (u8)v; }
static void w16(struct wr *w, u32 v) { w8(w, v >> 8); w8(w, v); }
static void wbytes(struct wr *w, const void *d, u32 n) { if (w->n + n <= w->cap) { memcpy(w->p + w->n, d, n); w->n += n; } }

static u32 build_client_hello(u8 *buf, u32 cap, const u8 random[32], const u8 sid[32], const u8 pub[32], const char *host)
{
    struct wr w = {buf, 0, cap};
    u32 hl = (u32)strlen(host), len_pos, ext_pos;
    int sni = hl && hl < 200 && !is_ipv4(host);
    w8(&w, HT_CLIENT_HELLO); w8(&w, 0); w16(&w, 0);              /* longueur patchee a la fin */
    w16(&w, 0x0303);
    wbytes(&w, random, 32);
    w8(&w, 32); wbytes(&w, sid, 32);
    w16(&w, 6); w16(&w, 0x1301); w16(&w, 0x1302); w16(&w, 0x1303);
    w8(&w, 1); w8(&w, 0);
    ext_pos = w.n; w16(&w, 0);
    if (sni) {
        w16(&w, 0); w16(&w, hl + 5); w16(&w, hl + 3); w8(&w, 0); w16(&w, hl); wbytes(&w, host, hl);
    }
    w16(&w, 0x000a); w16(&w, 4); w16(&w, 2); w16(&w, 0x001d);                         /* supported_groups : x25519 */
    w16(&w, 0x000d); w16(&w, 16); w16(&w, 14);                                        /* signature_algorithms */
    w16(&w, 0x0403); w16(&w, 0x0503); w16(&w, 0x0804); w16(&w, 0x0805); w16(&w, 0x0806); w16(&w, 0x0807); w16(&w, 0x0401);
    w16(&w, 0x002b); w16(&w, 3); w8(&w, 2); w16(&w, 0x0304);                          /* supported_versions : 1.3 */
    w16(&w, 0x0033); w16(&w, 38); w16(&w, 36); w16(&w, 0x001d); w16(&w, 32); wbytes(&w, pub, 32);   /* key_share */
    len_pos = w.n;
    w.p[ext_pos] = (u8)((len_pos - ext_pos - 2) >> 8); w.p[ext_pos + 1] = (u8)(len_pos - ext_pos - 2);
    w.p[2] = (u8)((w.n - 4) >> 8); w.p[3] = (u8)(w.n - 4);
    return w.n;
}

static int suite_params(int suite, int *hash, int *keylen)
{
    switch (suite) {
    case 0x1301: *hash = HASH_SHA256; *keylen = 16; return 0;
    case 0x1302: *hash = HASH_SHA384; *keylen = 32; return 0;
    case 0x1303: *hash = HASH_SHA256; *keylen = 32; return 0;
    }
    return -1;
}

static int verify_cv(struct tls *t, u32 scheme, const u8 *sig, u32 sl, const struct x509 *leaf)
{
    u8 content[64 + 34 + 1 + HASH_MAX], h[HASH_MAX], th[HASH_MAX];
    u32 n = 0;
    hash_ctx c;
    th_hash(t, th);
    memset(content, 0x20, 64); n = 64;
    memcpy(content + n, "TLS 1.3, server CertificateVerify", 33); n += 33;
    content[n++] = 0;
    memcpy(content + n, th, (u32)t->hashlen); n += (u32)t->hashlen;
    switch (scheme) {
    case 0x0403:
    case 0x0503: {
        int ha = scheme == 0x0403 ? HASH_SHA256 : HASH_SHA384;
        int curve = scheme == 0x0403 ? CURVE_P256 : CURVE_P384;
        if (leaf->key_type != (scheme == 0x0403 ? KEY_EC_P256 : KEY_EC_P384)) return -1;
        hash_init(&c, ha); hash_update(&c, content, n); hash_final(&c, h);
        return ecdsa_verify_der(curve, leaf->pub, leaf->publen, h, hash_len(ha), sig, sl);
    }
    case 0x0804: case 0x0805: case 0x0806: {
        int ha = scheme == 0x0804 ? HASH_SHA256 : scheme == 0x0805 ? HASH_SHA384 : HASH_SHA512;
        if (leaf->key_type != KEY_RSA) return -1;
        hash_init(&c, ha); hash_update(&c, content, n); hash_final(&c, h);
        return rsa_verify_pss(leaf->rsa_n, leaf->rsa_nlen, leaf->rsa_e, leaf->rsa_elen, ha, h, sig, sl);
    }
    case 0x0807:
        if (leaf->key_type != KEY_ED25519 || sl != 64) return -1;
        return ed25519_verify(sig, content, n, leaf->pub);
    }
    return -1;
}

static void finished_mac(struct tls *t, const u8 *base_secret, u8 *out)
{
    u8 fk[HASH_MAX], h[HASH_MAX];
    expand_label(t->hash, base_secret, "finished", NULL, 0, fk, (u32)t->hashlen);
    th_hash(t, h);
    hmac(t->hash, fk, (u32)t->hashlen, h, (u32)t->hashlen, out);
}

int tls_connect(struct tls *t, const struct tls_io *io, const char *host, u32 now)
{
    u8 priv[32], pub[32], random[32], sid[32], shared[32], ch[600], zero[HASH_MAX], early[HASH_MAX], derived[HASH_MAX],
       hs_secret[HASH_MAX], c_hs[HASH_MAX], s_hs[HASH_MAX], h0[HASH_MAX];
    u32 chlen, chosen_suite = 0;
    u8 mtype, rtype;
    const u8 *body;
    u32 blen, rlen;
    u8 *rdata;
    int r;
    const u8 *srv_pub = NULL;
    u8 spub_copy[32];
    int got_versions = 0;

    memset(t, 0, sizeof *t);
    t->io = *io;
    if (!rng_ready()) return TLS_E_RNG;

    rng_bytes(priv, 32); rng_bytes(random, 32); rng_bytes(sid, 32);
    x25519_base(pub, priv);
    chlen = build_client_hello(ch, sizeof ch, random, sid, pub, host);
    r = rec_write(t, CT_HANDSHAKE, ch, chlen);
    if (r) return r;

    /* ---- ServerHello ---- */
    r = rec_read(t, &rtype, &rdata, &rlen);
    if (r) return r;
    if (rtype == CT_ALERT) { if (rlen >= 2) t->alert = rdata[1]; return TLS_E_ALERT; }
    if (rtype != CT_HANDSHAKE || rlen < 4 || rdata[0] != HT_SERVER_HELLO) return fail(t, TLS_E_PROTOCOL, 10);
    {
        u32 mlen = (u32)rdata[1] << 16 | (u32)rdata[2] << 8 | rdata[3];
        const u8 *p = rdata + 4, *end;
        u32 sl, el;
        if (mlen + 4 != rlen) return fail(t, TLS_E_PROTOCOL, 50);       /* un seul message attendu dans cet enregistrement */
        end = p + mlen;
        if (mlen < 2 + 32 + 1 + 2 + 1 + 2) return fail(t, TLS_E_PROTOCOL, 50);
        p += 2;                                                         /* legacy_version */
        if (memcmp(p, HRR_RANDOM, 32) == 0) return fail(t, TLS_E_UNSUPPORTED, 40);
        p += 32;
        sl = *p++;
        if (sl != 32 || (u32)(end - p) < sl + 2 + 1 + 2 || memcmp(p, sid, 32) != 0) return fail(t, TLS_E_PROTOCOL, 47);
        p += sl;
        chosen_suite = (u32)p[0] << 8 | p[1]; p += 2;
        if (*p++ != 0) return fail(t, TLS_E_PROTOCOL, 47);
        el = (u32)p[0] << 8 | p[1]; p += 2;
        if ((u32)(end - p) != el) return fail(t, TLS_E_PROTOCOL, 50);
        while (p < end) {
            u32 type, l;
            if (end - p < 4) return fail(t, TLS_E_PROTOCOL, 50);
            type = (u32)p[0] << 8 | p[1]; l = (u32)p[2] << 8 | p[3]; p += 4;
            if ((u32)(end - p) < l) return fail(t, TLS_E_PROTOCOL, 50);
            if (type == 0x002b) {
                if (l != 2 || p[0] != 3 || p[1] != 4) return fail(t, TLS_E_UNSUPPORTED, 70);
                got_versions = 1;
            } else if (type == 0x0033) {
                if (l != 36 || p[0] != 0x00 || p[1] != 0x1d || p[2] != 0 || p[3] != 32) return fail(t, TLS_E_PROTOCOL, 47);
                memcpy(spub_copy, p + 4, 32);
                srv_pub = spub_copy;
            } else {
                return fail(t, TLS_E_PROTOCOL, 110);                   /* extension non demandee */
            }
            p += l;
        }
        if (!got_versions) return fail(t, TLS_E_UNSUPPORTED, 70);     /* serveur TLS 1.2 ou plus ancien */
        if (!srv_pub || suite_params((int)chosen_suite, &t->hash, &t->keylen)) return fail(t, TLS_E_PROTOCOL, 47);
        t->suite = (int)chosen_suite;
        t->hashlen = (int)hash_len(t->hash);
        hash_init(&t->th, t->hash);
        th_add(t, ch, chlen);
        th_add(t, rdata, rlen);
    }
    x25519(shared, priv, srv_pub);
    {   /* secret nul : point d'ordre faible */
        u8 acc = 0;
        for (int i = 0; i < 32; i++) acc |= shared[i];
        if (!acc) return fail(t, TLS_E_PROTOCOL, 47);
    }
    crypto_wipe(priv, sizeof priv);
    memset(zero, 0, sizeof zero);
    hkdf_extract(t->hash, NULL, 0, zero, (u32)t->hashlen, early);
    {
        hash_ctx e;
        hash_init(&e, t->hash); hash_final(&e, h0);                    /* condense de la chaine vide */
    }
    expand_label(t->hash, early, "derived", h0, (u32)t->hashlen, derived, (u32)t->hashlen);
    hkdf_extract(t->hash, derived, (u32)t->hashlen, shared, 32, hs_secret);
    crypto_wipe(shared, sizeof shared);
    derive_secret(t, hs_secret, "c hs traffic", c_hs);
    derive_secret(t, hs_secret, "s hs traffic", s_hs);
    expand_label(t->hash, hs_secret, "derived", h0, (u32)t->hashlen, derived, (u32)t->hashlen);
    hkdf_extract(t->hash, derived, (u32)t->hashlen, zero, (u32)t->hashlen, t->master);
    dir_set_secret(t, &t->rx, s_hs);

    /* ---- EncryptedExtensions ---- */
    r = hs_next(t, &mtype, &body, &blen);
    if (r) return fail(t, r, r == TLS_E_DECRYPT ? 20 : 10);
    if (mtype != HT_ENCRYPTED_EXTENSIONS || blen < 2) return fail(t, TLS_E_PROTOCOL, 10);
    if ((u32)((u32)body[0] << 8 | body[1]) != blen - 2) return fail(t, TLS_E_PROTOCOL, 50);
    th_add(t, t->hs, 4 + blen); hs_pop(t);

    /* ---- [CertificateRequest] Certificate CertificateVerify Finished ---- */
    {
        int want_empty_cert = 0;
        struct x509 leaf;
        const u8 *chain[8];
        u32 chainlen[8];
        int nchain = 0;
        r = hs_next(t, &mtype, &body, &blen);
        if (r) return fail(t, r, 10);
        if (mtype == HT_CERTIFICATE_REQUEST) {
            want_empty_cert = 1;
            th_add(t, t->hs, 4 + blen); hs_pop(t);
            r = hs_next(t, &mtype, &body, &blen);
            if (r) return fail(t, r, 10);
        }
        if (mtype != HT_CERTIFICATE || blen < 4 || body[0] != 0) return fail(t, TLS_E_PROTOCOL, 10);
        {
            u32 ll = (u32)body[1] << 16 | (u32)body[2] << 8 | body[3];
            const u8 *p = body + 4, *end = body + 4 + ll;
            if (ll + 4 != blen) return fail(t, TLS_E_PROTOCOL, 50);
            while (p < end) {
                u32 cl, xl;
                if (end - p < 3) return fail(t, TLS_E_PROTOCOL, 50);
                cl = (u32)p[0] << 16 | (u32)p[1] << 8 | p[2]; p += 3;
                if ((u32)(end - p) < cl + 2) return fail(t, TLS_E_PROTOCOL, 50);
                if (nchain < 8) { chain[nchain] = p; chainlen[nchain] = cl; nchain++; }
                p += cl;
                xl = (u32)p[0] << 8 | p[1]; p += 2;
                if ((u32)(end - p) < xl) return fail(t, TLS_E_PROTOCOL, 50);
                p += xl;
            }
        }
        if (nchain == 0) return fail(t, TLS_E_PROTOCOL, 42);
        r = x509_verify_chain(chain, chainlen, nchain, host, now);
        if (r) {
            t->cert_err = r;
            return fail(t, TLS_E_CERT, r == X509_E_EXPIRED || r == X509_E_NOTYET ? 45 : r == X509_E_UNTRUSTED ? 48 : 42);
        }
        if (x509_parse(&leaf, chain[0], chainlen[0])) return fail(t, TLS_E_CERT, 42);
        th_add(t, t->hs, 4 + blen);
        {
            /* le tampon hs va etre decale par hs_pop : on copie d'abord la cle publique du serveur */
            struct x509 lcopy = leaf;
            u8 keybuf[600];
            if (leaf.key_type == KEY_RSA) {
                if (leaf.rsa_nlen + leaf.rsa_elen > sizeof keybuf) return fail(t, TLS_E_CERT, 42);
                memcpy(keybuf, leaf.rsa_n, leaf.rsa_nlen); memcpy(keybuf + leaf.rsa_nlen, leaf.rsa_e, leaf.rsa_elen);
                lcopy.rsa_n = keybuf; lcopy.rsa_e = keybuf + leaf.rsa_nlen;
            } else {
                memcpy(keybuf, leaf.pub, leaf.publen);
                lcopy.pub = keybuf;
            }
            hs_pop(t);
            r = hs_next(t, &mtype, &body, &blen);
            if (r) return fail(t, r, 10);
            if (mtype != HT_CERTIFICATE_VERIFY || blen < 4) return fail(t, TLS_E_PROTOCOL, 10);
            {
                u32 scheme = (u32)body[0] << 8 | body[1], sl = (u32)body[2] << 8 | body[3];
                if (sl + 4 != blen) return fail(t, TLS_E_PROTOCOL, 50);
                if (verify_cv(t, scheme, body + 4, sl, &lcopy)) return fail(t, TLS_E_VERIFY, 51);
            }
        }
        th_add(t, t->hs, 4 + blen); hs_pop(t);
        r = hs_next(t, &mtype, &body, &blen);
        if (r) return fail(t, r, 10);
        if (mtype != HT_FINISHED || blen != (u32)t->hashlen) return fail(t, TLS_E_PROTOCOL, 10);
        {
            u8 expect[HASH_MAX];
            finished_mac(t, s_hs, expect);
            if (!crypto_memeq(expect, body, (u32)t->hashlen)) return fail(t, TLS_E_VERIFY, 51);
        }
        th_add(t, t->hs, 4 + blen); hs_pop(t);
        if (t->hs_len) return fail(t, TLS_E_PROTOCOL, 10);             /* rien ne doit suivre Finished avant nous */
        /* secrets applicatifs : condense jusqu'au Finished du serveur */
        {
            u8 c_ap[HASH_MAX], s_ap[HASH_MAX];
            derive_secret(t, t->master, "c ap traffic", c_ap);
            derive_secret(t, t->master, "s ap traffic", s_ap);
            /* notre reponse : compatibilite, certificat vide si demande, puis Finished chiffre avec les cles de poignee */
            {
                static const u8 ccs[6] = {CT_CCS, 3, 3, 0, 1, 1};
                r = io_send(t, ccs, 6);
                if (r) return r;
            }
            dir_set_secret(t, &t->tx, c_hs);
            if (want_empty_cert) {
                static const u8 emptycert[8] = {HT_CERTIFICATE, 0, 0, 4, 0, 0, 0, 0};
                r = rec_write(t, CT_HANDSHAKE, emptycert, 8);
                if (r) return r;
                th_add(t, emptycert, 8);
            }
            {
                u8 fin[4 + HASH_MAX];
                fin[0] = HT_FINISHED; fin[1] = 0; fin[2] = 0; fin[3] = (u8)t->hashlen;
                finished_mac(t, c_hs, fin + 4);
                r = rec_write(t, CT_HANDSHAKE, fin, 4 + (u32)t->hashlen);
                if (r) return r;
            }
            dir_set_secret(t, &t->rx, s_ap);
            dir_set_secret(t, &t->tx, c_ap);
            crypto_wipe(c_ap, sizeof c_ap); crypto_wipe(s_ap, sizeof s_ap);
        }
    }
    crypto_wipe(hs_secret, sizeof hs_secret);
    crypto_wipe(c_hs, sizeof c_hs); crypto_wipe(s_hs, sizeof s_hs);
    t->established = 1;
    return TLS_OK;
}

/* ------------------------------------------------------------------ */
/* Donnees                                                             */
/* ------------------------------------------------------------------ */
static int rekey(struct tls *t, struct tls_dir *d)
{
    u8 next[HASH_MAX];
    expand_label(t->hash, d->secret, "traffic upd", NULL, 0, next, (u32)t->hashlen);
    dir_set_secret(t, d, next);
    crypto_wipe(next, sizeof next);
    return TLS_OK;
}

static int post_handshake(struct tls *t, const u8 *d, u32 n)
{
    while (n) {
        u32 l;
        if (n < 4) return fail(t, TLS_E_PROTOCOL, 50);
        l = (u32)d[1] << 16 | (u32)d[2] << 8 | d[3];
        if (l + 4 > n) return fail(t, TLS_E_PROTOCOL, 50);
        if (d[0] == HT_NEW_SESSION_TICKET) {
            /* ignore : pas de reprise de session */
        } else if (d[0] == HT_KEY_UPDATE) {
            if (l != 1 || d[4] > 1) return fail(t, TLS_E_PROTOCOL, 47);
            rekey(t, &t->rx);
            if (d[4] == 1) {                                          /* le serveur demande que nous changions aussi */
                u8 ku[5] = {HT_KEY_UPDATE, 0, 0, 1, 0};
                int r = rec_write(t, CT_HANDSHAKE, ku, 5);
                if (r) return fail(t, r, 0);
                rekey(t, &t->tx);
            }
        } else {
            return fail(t, TLS_E_PROTOCOL, 10);
        }
        d += 4 + l; n -= 4 + l;
    }
    return TLS_OK;
}

int tls_send(struct tls *t, const u8 *data, u32 len)
{
    u32 done = 0;
    if (!t->established || t->failed) return TLS_E_CLOSED;
    if (t->closed) return TLS_E_CLOSED;
    while (done < len) {
        u32 k = len - done < TLS_REC_MAX ? len - done : TLS_REC_MAX;
        int r = rec_write(t, CT_APPDATA, data + done, k);
        if (r) { t->failed = 1; return r; }
        done += k;
    }
    return (int)len;
}

int tls_recv(struct tls *t, u8 *buf, u32 cap)
{
    if (!t->established || t->failed) return TLS_E_CLOSED;
    if (cap == 0) return 0;
    for (;;) {
        if (t->app_len) {
            u32 k = t->app_len < cap ? t->app_len : cap;
            memcpy(buf, t->app, k);
            t->app += k; t->app_len -= k;
            return (int)k;
        }
        if (t->closed) return 0;
        {
            u8 type;
            u8 *d;
            u32 n;
            int r = rec_read(t, &type, &d, &n);
            if (r) {
                if (r == TLS_E_IO) { t->failed = 1; return TLS_E_IO; }       /* coupure sans close_notify : on ne la prend pas pour une fin normale */
                return fail(t, r, r == TLS_E_DECRYPT ? 20 : 10);
            }
            if (type == CT_APPDATA) {
                t->app = d; t->app_len = n;
                continue;
            }
            if (type == CT_ALERT) {
                if (n != 2) return fail(t, TLS_E_PROTOCOL, 50);
                if (d[1] == 0) { t->closed = 1; return 0; }                  /* close_notify */
                t->alert = d[1];
                t->failed = 1;
                return TLS_E_ALERT;
            }
            if (type == CT_HANDSHAKE) {
                r = post_handshake(t, d, n);
                if (r) return r;
                continue;
            }
            return fail(t, TLS_E_PROTOCOL, 10);
        }
    }
}

void tls_close(struct tls *t)
{
    if (t->established && !t->closed && !t->failed) {
        u8 a[2] = {1, 0};
        (void)rec_write(t, CT_ALERT, a, 2);
    }
    t->closed = 1;
}

const char *tls_strerror(int e)
{
    switch (e) {
    case TLS_OK: return "ok";
    case TLS_E_IO: return "connexion coupee";
    case TLS_E_PROTOCOL: return "reponse du serveur incorrecte";
    case TLS_E_ALERT: return "alerte du serveur";
    case TLS_E_CERT: return "certificat refuse";
    case TLS_E_VERIFY: return "signature du serveur fausse";
    case TLS_E_UNSUPPORTED: return "le serveur n'accepte pas TLS 1.3";
    case TLS_E_RNG: return "generateur aleatoire pas pret";
    case TLS_E_DECRYPT: return "donnees corrompues ou falsifiees";
    case TLS_E_CLOSED: return "connexion fermee";
    case TLS_E_TOOBIG: return "message du serveur trop grand";
    }
    return "erreur TLS inconnue";
}
