/* Grands entiers (Montgomery, mots de 32 bits), RSA et ECDSA : verification de signatures seulement. */
#include "pubkey.h"
#include "crypto.h"
#include "string.h"

#define BN_MAX 130                      /* 4160 bits : de quoi faire RSA-4096 */

typedef struct {
    u32 n[BN_MAX], rr[BN_MAX];          /* module et R^2 mod n (R = 2^(32*len)) */
    u32 n0inv;                          /* -n^-1 mod 2^32 */
    int len;
} mont;

static int bn_cmp(const u32 *a, const u32 *b, int len)
{
    for (int i = len - 1; i >= 0; i--)
        if (a[i] != b[i])
            return a[i] > b[i] ? 1 : -1;
    return 0;
}

static u32 bn_sub(u32 *r, const u32 *a, const u32 *b, int len)    /* r = a - b ; renvoie l'emprunt */
{
    u64 borrow = 0;
    for (int i = 0; i < len; i++) {
        u64 d = (u64)a[i] - b[i] - borrow;
        r[i] = (u32)d;
        borrow = (d >> 32) & 1;
    }
    return (u32)borrow;
}

static u32 bn_add(u32 *r, const u32 *a, const u32 *b, int len)    /* r = a + b ; renvoie la retenue */
{
    u64 c = 0;
    for (int i = 0; i < len; i++) {
        u64 s = (u64)a[i] + b[i] + c;
        r[i] = (u32)s;
        c = s >> 32;
    }
    return (u32)c;
}

static int bn_is_zero(const u32 *a, int len)
{
    u32 x = 0;
    for (int i = 0; i < len; i++) x |= a[i];
    return x == 0;
}

/* Octets gros-boutistes -> mots (len mots, complete par des zeros). -1 si trop grand. */
static int bn_from_be(u32 *r, int len, const u8 *b, u32 n)
{
    while (n && *b == 0) { b++; n--; }
    if (n > (u32)len * 4)
        return -1;
    for (int i = 0; i < len; i++) r[i] = 0;
    for (u32 i = 0; i < n; i++)
        r[i / 4] |= (u32)b[n - 1 - i] << (8 * (i % 4));
    return 0;
}

static void bn_to_be(u8 *out, u32 n, const u32 *a)               /* n octets, poids fort d'abord */
{
    for (u32 i = 0; i < n; i++)
        out[n - 1 - i] = (u8)(a[i / 4] >> (8 * (i % 4)));
}

static int bn_bits(const u32 *a, int len)
{
    for (int i = len - 1; i >= 0; i--)
        if (a[i]) {
            int b = 32;
            while (!(a[i] >> (b - 1))) b--;
            return i * 32 + b;
        }
    return 0;
}

/* Prepare le contexte pour un module impair n (len mots, n[len-1] != 0). */
static int mont_init(mont *m, const u32 *n, int len)
{
    u32 inv;
    if (!(n[0] & 1) || len < 1 || len > BN_MAX || n[len - 1] == 0)
        return -1;
    m->len = len;
    for (int i = 0; i < BN_MAX; i++) { m->n[i] = i < len ? n[i] : 0; m->rr[i] = 0; }
    inv = n[0];
    for (int i = 0; i < 5; i++) inv *= 2 - n[0] * inv;            /* inverse de n[0] modulo 2^32 (Newton) */
    m->n0inv = (u32)0 - inv;
    /* R^2 mod n : on part de 1 et on double 64*len fois, en soustrayant n quand on le depasse */
    {
        u32 x[BN_MAX];
        for (int i = 0; i < len; i++) x[i] = 0;
        x[0] = 1;
        for (int k = 0; k < 64 * len; k++) {
            u32 carry = bn_add(x, x, x, len);
            if (carry || bn_cmp(x, m->n, len) >= 0)
                bn_sub(x, x, m->n, len);
        }
        for (int i = 0; i < len; i++) m->rr[i] = x[i];
    }
    return 0;
}

/* r = a * b / R mod n (a, b < n). r peut etre a ou b. */
static void mont_mul(const mont *m, u32 *r, const u32 *a, const u32 *b)
{
    int n = m->len;
    u32 t[BN_MAX + 2];
    for (int i = 0; i < n + 2; i++) t[i] = 0;
    for (int i = 0; i < n; i++) {
        u64 c = 0, s;
        u32 q;
        for (int j = 0; j < n; j++) {
            s = (u64)t[j] + (u64)a[j] * b[i] + c;
            t[j] = (u32)s;
            c = s >> 32;
        }
        s = (u64)t[n] + c;
        t[n] = (u32)s;
        t[n + 1] = (u32)(s >> 32);
        q = t[0] * m->n0inv;
        s = (u64)t[0] + (u64)q * m->n[0];
        c = s >> 32;
        for (int j = 1; j < n; j++) {
            s = (u64)t[j] + (u64)q * m->n[j] + c;
            t[j - 1] = (u32)s;
            c = s >> 32;
        }
        s = (u64)t[n] + c;
        t[n - 1] = (u32)s;
        t[n] = t[n + 1] + (u32)(s >> 32);
    }
    if (t[n] || bn_cmp(t, m->n, n) >= 0)
        bn_sub(t, t, m->n, n);
    for (int i = 0; i < n; i++) r[i] = t[i];
}

static void mont_to(const mont *m, u32 *r, const u32 *a) { mont_mul(m, r, a, m->rr); }
static void mont_from(const mont *m, u32 *r, const u32 *a)
{
    u32 one[BN_MAX];
    for (int i = 0; i < m->len; i++) one[i] = 0;
    one[0] = 1;
    mont_mul(m, r, a, one);
}

/* r = base^exp, tout en domaine Montgomery (base et r) ; exp en octets gros-boutistes (public). */
static void mont_exp_m(const mont *m, u32 *r, const u32 *baseM, const u8 *exp, u32 elen)
{
    u32 acc[BN_MAX], one[BN_MAX];
    int n = m->len;
    for (int i = 0; i < n; i++) one[i] = 0;
    one[0] = 1;
    mont_to(m, acc, one);
    for (u32 i = 0; i < elen; i++)
        for (int bit = 7; bit >= 0; bit--) {
            mont_mul(m, acc, acc, acc);
            if ((exp[i] >> bit) & 1)
                mont_mul(m, acc, acc, baseM);
        }
    for (int i = 0; i < n; i++) r[i] = acc[i];
}

/* r = base^exp mod n ; base < n, domaine normal. */
static void mont_exp(const mont *m, u32 *r, const u32 *base, const u8 *exp, u32 elen)
{
    u32 b[BN_MAX], acc[BN_MAX];
    mont_to(m, b, base);
    mont_exp_m(m, acc, b, exp, elen);
    mont_from(m, r, acc);
}

/* ================= RSA ================= */
static const u8 DI_SHA256[19] = {0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20};
static const u8 DI_SHA384[19] = {0x30,0x41,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x02,0x05,0x00,0x04,0x30};
static const u8 DI_SHA512[19] = {0x30,0x51,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x03,0x05,0x00,0x04,0x40};

/* em = sig^e mod n sur k octets (k = taille du module). 0 si ok. */
static int rsa_public(u8 *em, u32 *bits, const u8 *n, u32 nlen, const u8 *e, u32 elen, const u8 *sig, u32 siglen)
{
    static mont m;
    u32 nl[BN_MAX], s[BN_MAX], r[BN_MAX];
    int len;
    while (nlen && *n == 0) { n++; nlen--; }
    if (nlen < 128 || nlen > 512 || !elen || elen > 4 || siglen != nlen)
        return -1;
    len = (int)((nlen + 3) / 4);
    if (bn_from_be(nl, len, n, nlen) || bn_from_be(s, len, sig, siglen))
        return -1;
    if (bn_cmp(s, nl, len) >= 0)
        return -1;
    {   /* e doit etre impair et > 1 */
        u32 ev = 0;
        for (u32 i = 0; i < elen; i++) ev = ev << 8 | e[i];
        if (ev < 3 || !(ev & 1))
            return -1;
    }
    if (mont_init(&m, nl, len))
        return -1;
    mont_exp(&m, r, s, e, elen);
    bn_to_be(em, nlen, r);
    *bits = (u32)bn_bits(nl, len);
    return 0;
}

int rsa_verify_pkcs1(const u8 *n, u32 nlen, const u8 *e, u32 elen, int hashalg, const u8 *hash, const u8 *sig, u32 siglen)
{
    u8 em[512];
    u32 bits, hl = hash_len(hashalg), k, pad;
    const u8 *di = hashalg == HASH_SHA256 ? DI_SHA256 : hashalg == HASH_SHA384 ? DI_SHA384 : hashalg == HASH_SHA512 ? DI_SHA512 : NULL;
    if (!di)
        return -1;
    if (rsa_public(em, &bits, n, nlen, e, elen, sig, siglen))
        return -1;
    while (nlen && *n == 0) { n++; nlen--; }
    k = nlen;
    if (k < 19 + hl + 11)
        return -1;
    /* 00 01 FF..FF 00 DigestInfo hash */
    pad = k - 3 - 19 - hl;
    if (em[0] != 0 || em[1] != 1)
        return -1;
    for (u32 i = 0; i < pad; i++)
        if (em[2 + i] != 0xff)
            return -1;
    if (em[2 + pad] != 0)
        return -1;
    if (!crypto_memeq(em + 3 + pad, di, 19) || !crypto_memeq(em + 3 + pad + 19, hash, hl))
        return -1;
    return 0;
}

static void mgf1(int alg, const u8 *seed, u32 slen, u8 *out, u32 olen)
{
    u32 hl = hash_len(alg), done = 0;
    u8 h[HASH_MAX];
    for (u32 ctr = 0; done < olen; ctr++) {
        hash_ctx c;
        u8 cb[4] = {(u8)(ctr >> 24), (u8)(ctr >> 16), (u8)(ctr >> 8), (u8)ctr};
        u32 take = olen - done < hl ? olen - done : hl;
        hash_init(&c, alg); hash_update(&c, seed, slen); hash_update(&c, cb, 4); hash_final(&c, h);
        memcpy(out + done, h, take);
        done += take;
    }
}

int rsa_verify_pss(const u8 *n, u32 nlen, const u8 *e, u32 elen, int hashalg, const u8 *hash, const u8 *sig, u32 siglen)
{
    u8 em[512], db[512], mp[8 + HASH_MAX + HASH_MAX], h2[HASH_MAX];
    u32 bits, hl = hash_len(hashalg), emlen, dblen, topbits;
    const u8 *h, *masked;
    hash_ctx c;
    if (!hl)
        return -1;
    if (rsa_public(em, &bits, n, nlen, e, elen, sig, siglen))
        return -1;
    while (nlen && *n == 0) { n++; nlen--; }
    emlen = (bits - 1 + 7) / 8;                         /* emBits = modBits - 1 */
    {   /* em est sur nlen octets ; si emBits tombe sur un multiple de 8, le premier octet est a ignorer (zero) */
        u32 skip = nlen - emlen;
        for (u32 i = 0; i < skip; i++)
            if (em[i] != 0)
                return -1;
        memmove(em, em + skip, emlen);
    }
    if (emlen < hl + hl + 2 || em[emlen - 1] != 0xbc)
        return -1;
    dblen = emlen - hl - 1;
    masked = em;
    h = em + dblen;
    topbits = 8 * emlen - (bits - 1);
    if (masked[0] & (u8)(0xff << (8 - topbits)) && topbits)
        return -1;
    mgf1(hashalg, h, hl, db, dblen);
    for (u32 i = 0; i < dblen; i++) db[i] ^= masked[i];
    if (topbits)
        db[0] &= (u8)(0xff >> topbits);
    {
        u32 zeros = dblen - hl - 1;                     /* sel de la taille du condense */
        for (u32 i = 0; i < zeros; i++)
            if (db[i] != 0)
                return -1;
        if (db[zeros] != 1)
            return -1;
    }
    memset(mp, 0, 8);
    memcpy(mp + 8, hash, hl);
    memcpy(mp + 8 + hl, db + dblen - hl, hl);
    hash_init(&c, hashalg); hash_update(&c, mp, 8 + 2 * hl); hash_final(&c, h2);
    return crypto_memeq(h2, h, hl) ? 0 : -1;
}

/* ================= ECDSA ================= */
#include "curves.inc"

#define EC_MAX 12                                       /* P-384 : 12 mots */

typedef struct {
    int len;                                            /* mots */
    u32 bytes;
    mont p, n;
    u32 b[EC_MAX];                                      /* b en domaine Montgomery (mod p) */
    u32 gx[EC_MAX], gy[EC_MAX];                         /* G, domaine Montgomery */
    u32 one[EC_MAX];                                    /* 1 en domaine Montgomery */
} curve;

typedef struct { u32 x[EC_MAX], y[EC_MAX], z[EC_MAX]; } jpt;    /* Jacobien, Montgomery ; z = 0 : infini */

static int curve_init(curve *c, int id)
{
    u32 tmp[EC_MAX];
    const u8 *P, *N, *B, *GX, *GY;
    if (id == CURVE_P256) { c->bytes = 32; P = P256_P; N = P256_N; B = P256_B; GX = P256_GX; GY = P256_GY; }
    else if (id == CURVE_P384) { c->bytes = 48; P = P384_P; N = P384_N; B = P384_B; GX = P384_GX; GY = P384_GY; }
    else return -1;
    c->len = (int)(c->bytes / 4);
    bn_from_be(tmp, c->len, P, c->bytes);
    if (mont_init(&c->p, tmp, c->len)) return -1;
    bn_from_be(tmp, c->len, N, c->bytes);
    if (mont_init(&c->n, tmp, c->len)) return -1;
    bn_from_be(tmp, c->len, B, c->bytes);  mont_to(&c->p, c->b, tmp);
    bn_from_be(tmp, c->len, GX, c->bytes); mont_to(&c->p, c->gx, tmp);
    bn_from_be(tmp, c->len, GY, c->bytes); mont_to(&c->p, c->gy, tmp);
    for (int i = 0; i < c->len; i++) tmp[i] = 0;
    tmp[0] = 1;
    mont_to(&c->p, c->one, tmp);
    return 0;
}

static void fadd(const curve *c, u32 *r, const u32 *a, const u32 *b)
{
    u32 carry = bn_add(r, a, b, c->len);
    if (carry || bn_cmp(r, c->p.n, c->len) >= 0)
        bn_sub(r, r, c->p.n, c->len);
}

static void fsub(const curve *c, u32 *r, const u32 *a, const u32 *b)
{
    if (bn_sub(r, a, b, c->len))
        bn_add(r, r, c->p.n, c->len);
}

static void fmul(const curve *c, u32 *r, const u32 *a, const u32 *b) { mont_mul(&c->p, r, a, b); }

static void jset_inf(const curve *c, jpt *p)
{
    for (int i = 0; i < c->len; i++) { p->x[i] = c->one[i]; p->y[i] = c->one[i]; p->z[i] = 0; }
}

static void jdouble(const curve *c, jpt *r, const jpt *p)
{
    u32 delta[EC_MAX], gamma[EC_MAX], beta[EC_MAX], alpha[EC_MAX], t1[EC_MAX], t2[EC_MAX], x3[EC_MAX], y3[EC_MAX], z3[EC_MAX];
    if (bn_is_zero(p->z, c->len)) { *r = *p; return; }
    fmul(c, delta, p->z, p->z);
    fmul(c, gamma, p->y, p->y);
    fmul(c, beta, p->x, gamma);
    fsub(c, t1, p->x, delta);
    fadd(c, t2, p->x, delta);
    fmul(c, alpha, t1, t2);
    fadd(c, t1, alpha, alpha); fadd(c, alpha, t1, alpha);       /* alpha = 3 (x - delta)(x + delta) */
    fmul(c, t1, alpha, alpha);
    fadd(c, t2, beta, beta); fadd(c, t2, t2, t2); fadd(c, t2, t2, t2);   /* 8 beta */
    fsub(c, x3, t1, t2);
    fadd(c, t1, p->y, p->z);
    fmul(c, t1, t1, t1);
    fsub(c, t1, t1, gamma);
    fsub(c, z3, t1, delta);
    fadd(c, t1, beta, beta); fadd(c, t1, t1, t1);               /* 4 beta */
    fsub(c, t1, t1, x3);
    fmul(c, t1, alpha, t1);
    fmul(c, t2, gamma, gamma);
    fadd(c, t2, t2, t2); fadd(c, t2, t2, t2); fadd(c, t2, t2, t2);       /* 8 gamma^2 */
    fsub(c, y3, t1, t2);
    for (int i = 0; i < c->len; i++) { r->x[i] = x3[i]; r->y[i] = y3[i]; r->z[i] = z3[i]; }
}

static void jadd(const curve *c, jpt *r, const jpt *p, const jpt *q)
{
    u32 z1z1[EC_MAX], z2z2[EC_MAX], u1[EC_MAX], u2[EC_MAX], s1[EC_MAX], s2[EC_MAX], h[EC_MAX], i[EC_MAX], j[EC_MAX],
        rr[EC_MAX], v[EC_MAX], t[EC_MAX], x3[EC_MAX], y3[EC_MAX], z3[EC_MAX];
    if (bn_is_zero(p->z, c->len)) { *r = *q; return; }
    if (bn_is_zero(q->z, c->len)) { *r = *p; return; }
    fmul(c, z1z1, p->z, p->z);
    fmul(c, z2z2, q->z, q->z);
    fmul(c, u1, p->x, z2z2);
    fmul(c, u2, q->x, z1z1);
    fmul(c, t, q->z, z2z2); fmul(c, s1, p->y, t);
    fmul(c, t, p->z, z1z1); fmul(c, s2, q->y, t);
    fsub(c, h, u2, u1);
    fsub(c, rr, s2, s1);
    if (bn_is_zero(h, c->len)) {
        if (bn_is_zero(rr, c->len)) { jdouble(c, r, p); return; }
        jset_inf(c, r);
        return;
    }
    fadd(c, rr, rr, rr);
    fadd(c, t, h, h); fmul(c, i, t, t);
    fmul(c, j, h, i);
    fmul(c, v, u1, i);
    fmul(c, x3, rr, rr);
    fsub(c, x3, x3, j);
    fadd(c, t, v, v);
    fsub(c, x3, x3, t);
    fsub(c, t, v, x3);
    fmul(c, y3, rr, t);
    fmul(c, t, s1, j); fadd(c, t, t, t);
    fsub(c, y3, y3, t);
    fadd(c, t, p->z, q->z); fmul(c, t, t, t);
    fsub(c, t, t, z1z1); fsub(c, t, t, z2z2);
    fmul(c, z3, t, h);
    for (int k = 0; k < c->len; k++) { r->x[k] = x3[k]; r->y[k] = y3[k]; r->z[k] = z3[k]; }
}

/* x affine du point, en domaine normal. -1 si c'est l'infini. */
static int jto_x(const curve *c, u32 *xout, const jpt *p)
{
    u32 zi[EC_MAX], zi2[EC_MAX], xa[EC_MAX];
    u8 pm2[48];
    if (bn_is_zero(p->z, c->len))
        return -1;
    bn_to_be(pm2, c->bytes, c->p.n);
    pm2[c->bytes - 1] -= 2;                              /* p - 2 (p se termine par 0xff) */
    mont_exp_m(&c->p, zi, p->z, pm2, c->bytes);          /* z^-1 */
    fmul(c, zi2, zi, zi);
    fmul(c, xa, p->x, zi2);
    mont_from(&c->p, xout, xa);
    return 0;
}

static int on_curve(const curve *c, const u32 *xM, const u32 *yM)
{
    u32 l[EC_MAX], r[EC_MAX], t[EC_MAX];
    fmul(c, l, yM, yM);
    fmul(c, t, xM, xM);
    fmul(c, r, t, xM);
    fadd(c, t, xM, xM); fadd(c, t, t, xM);               /* 3x */
    fsub(c, r, r, t);
    fadd(c, r, r, c->b);
    return bn_cmp(l, r, c->len) == 0;
}

static u32 mulmod_n(const curve *c, u32 *r, const u32 *a, const u32 *b)
{
    u32 t[EC_MAX];
    mont_mul(&c->n, t, a, b);
    mont_mul(&c->n, r, t, c->n.rr);
    return 0;
}

static int der_len(const u8 **p, const u8 *end, u32 *len)
{
    u32 l;
    if (*p >= end) return -1;
    l = *(*p)++;
    if (l & 0x80) {
        u32 k = l & 0x7f;
        if (k == 0 || k > 2 || (u32)(end - *p) < k) return -1;
        l = 0;
        while (k--) l = l << 8 | *(*p)++;
    }
    if (l > (u32)(end - *p)) return -1;
    *len = l;
    return 0;
}

static int der_uint(const u8 **p, const u8 *end, const u8 **v, u32 *vl)
{
    u32 l;
    if (*p >= end || *(*p)++ != 0x02 || der_len(p, end, &l) || l == 0) return -1;
    if (**p & 0x80) return -1;                           /* negatif */
    *v = *p; *vl = l; *p += l;
    return 0;
}

int ecdsa_verify_der(int curve_id, const u8 *pub, u32 publen, const u8 *hash, u32 hlen, const u8 *sig, u32 siglen)
{
    static curve c;
    u32 r[EC_MAX], sv[EC_MAX], e[EC_MAX], w[EC_MAX], u1[EC_MAX], u2[EC_MAX], x[EC_MAX], tmp[EC_MAX];
    jpt G, Q, GQ, acc;
    const u8 *p = sig, *end = sig + siglen, *rb, *sb;
    u32 l, rl, sl, hb;
    u8 nm2[48];
    int nbits;

    if (curve_init(&c, curve_id) || publen != 1 + 2 * c.bytes || pub[0] != 4)
        return -1;
    /* signature : SEQUENCE { INTEGER r, INTEGER s } */
    if (p >= end || *p++ != 0x30 || der_len(&p, end, &l) || p + l != end)
        return -1;
    if (der_uint(&p, end, &rb, &rl) || der_uint(&p, end, &sb, &sl) || p != end)
        return -1;
    if (bn_from_be(r, c.len, rb, rl) || bn_from_be(sv, c.len, sb, sl))
        return -1;
    if (bn_is_zero(r, c.len) || bn_is_zero(sv, c.len) || bn_cmp(r, c.n.n, c.len) >= 0 || bn_cmp(sv, c.n.n, c.len) >= 0)
        return -1;
    /* cle publique : coordonnees < p et point sur la courbe */
    bn_from_be(tmp, c.len, pub + 1, c.bytes);
    if (bn_cmp(tmp, c.p.n, c.len) >= 0) return -1;
    mont_to(&c.p, Q.x, tmp);
    bn_from_be(tmp, c.len, pub + 1 + c.bytes, c.bytes);
    if (bn_cmp(tmp, c.p.n, c.len) >= 0) return -1;
    mont_to(&c.p, Q.y, tmp);
    for (int i = 0; i < c.len; i++) Q.z[i] = c.one[i];
    if (!on_curve(&c, Q.x, Q.y))
        return -1;
    for (int i = 0; i < c.len; i++) { G.x[i] = c.gx[i]; G.y[i] = c.gy[i]; G.z[i] = c.one[i]; }
    /* e = condense tronque a la taille de l'ordre, modulo n */
    hb = hlen < c.bytes ? hlen : c.bytes;
    if (bn_from_be(e, c.len, hash, hb))
        return -1;
    if (bn_cmp(e, c.n.n, c.len) >= 0)
        bn_sub(e, e, c.n.n, c.len);
    /* w = s^-1 mod n ; u1 = e w ; u2 = r w */
    bn_to_be(nm2, c.bytes, c.n.n);
    nm2[c.bytes - 1] -= 2;
    mont_to(&c.n, tmp, sv);
    mont_exp_m(&c.n, w, tmp, nm2, c.bytes);              /* w en domaine Montgomery */
    mont_from(&c.n, w, w);
    mulmod_n(&c, u1, e, w);
    mulmod_n(&c, u2, r, w);
    /* R = u1 G + u2 Q (methode de Shamir) */
    jadd(&c, &GQ, &G, &Q);
    jset_inf(&c, &acc);
    nbits = (int)c.bytes * 8;
    for (int i = nbits - 1; i >= 0; i--) {
        int b1 = (int)((u1[i / 32] >> (i % 32)) & 1), b2 = (int)((u2[i / 32] >> (i % 32)) & 1);
        jdouble(&c, &acc, &acc);
        if (b1 && b2) jadd(&c, &acc, &acc, &GQ);
        else if (b1)  jadd(&c, &acc, &acc, &G);
        else if (b2)  jadd(&c, &acc, &acc, &Q);
    }
    if (jto_x(&c, x, &acc))
        return -1;
    if (bn_cmp(x, c.n.n, c.len) >= 0)
        bn_sub(x, x, c.n.n, c.len);
    return bn_cmp(x, r, c.len) == 0 ? 0 : -1;
}
