/* Courbe 25519 : X25519 (RFC 7748) et Ed25519 (RFC 8032).
 * Arithmetique en 16 morceaux de 16 bits, d'apres TweetNaCl (domaine public) : compacte, a temps constant,
 * sans division 64 bits. Les constantes ci-dessous ont ete recalculees (tests/gen_crypto_vectors.py les verifie). */
#include "crypto.h"
#include "string.h"

typedef i64 gf[16];

static const gf gf0;
static const gf gf1 = {1};
static const gf C121665 = {0xdb41, 1};
static const gf D = {0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070, 0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203};
static const gf D2 = {0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0, 0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406};
static const gf GX = {0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c, 0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169};
static const gf GY = {0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666};
static const gf GI = {0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43, 0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83};
static const i64 LL[32] = {237, 211, 245, 92, 26, 99, 18, 88, 214, 156, 247, 162, 222, 249, 222, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16};

static void car25519(gf o)
{
    i64 c;
    for (int i = 0; i < 16; i++) {
        o[i] += (1LL << 16);
        c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}

static void sel25519(gf p, gf q, int b)
{
    i64 t, c = ~((i64)b - 1);
    for (int i = 0; i < 16; i++) {
        t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static void pack25519(u8 *o, const gf n)
{
    int b;
    gf m, t;
    for (int i = 0; i < 16; i++) t[i] = n[i];
    car25519(t); car25519(t); car25519(t);
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        sel25519(t, m, 1 - b);
    }
    for (int i = 0; i < 16; i++) {
        o[2 * i] = (u8)(t[i] & 0xff);
        o[2 * i + 1] = (u8)(t[i] >> 8);
    }
}

static int neq25519(const gf a, const gf b)
{
    u8 c[32], d[32];
    pack25519(c, a);
    pack25519(d, b);
    return !crypto_memeq(c, d, 32);
}

static u8 par25519(const gf a)
{
    u8 d[32];
    pack25519(d, a);
    return d[0] & 1;
}

static void unpack25519(gf o, const u8 *n)
{
    for (int i = 0; i < 16; i++) o[i] = n[2 * i] + ((i64)n[2 * i + 1] << 8);
    o[15] &= 0x7fff;
}

static void A(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] + b[i]; }
static void Z(gf o, const gf a, const gf b) { for (int i = 0; i < 16; i++) o[i] = a[i] - b[i]; }

static void M(gf o, const gf a, const gf b)
{
    i64 t[31];
    for (int i = 0; i < 31; i++) t[i] = 0;
    for (int i = 0; i < 16; i++)
        for (int j = 0; j < 16; j++)
            t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; i++) o[i] = t[i];
    car25519(o);
    car25519(o);
}

static void S(gf o, const gf a) { M(o, a, a); }

static void inv25519(gf o, const gf i)
{
    gf c;
    for (int a = 0; a < 16; a++) c[a] = i[a];
    for (int a = 253; a >= 0; a--) {
        S(c, c);
        if (a != 2 && a != 4) M(c, c, i);
    }
    for (int a = 0; a < 16; a++) o[a] = c[a];
}

static void pow2523(gf o, const gf i)
{
    gf c;
    for (int a = 0; a < 16; a++) c[a] = i[a];
    for (int a = 250; a >= 0; a--) {
        S(c, c);
        if (a != 1) M(c, c, i);
    }
    for (int a = 0; a < 16; a++) o[a] = c[a];
}

/* ---- X25519 ---- */
void x25519(u8 out[32], const u8 scalar[32], const u8 point[32])
{
    u8 z[32];
    i64 x[80], r;
    gf a, b, c, d, e, f;
    memcpy(z, scalar, 32);
    z[31] = (u8)((z[31] & 127) | 64);
    z[0] &= 248;
    unpack25519(x, point);
    for (int i = 0; i < 16; i++) { b[i] = x[i]; d[i] = a[i] = c[i] = 0; }
    a[0] = d[0] = 1;
    for (int i = 254; i >= 0; --i) {
        r = (z[i >> 3] >> (i & 7)) & 1;
        sel25519(a, b, (int)r);
        sel25519(c, d, (int)r);
        A(e, a, c); Z(a, a, c); A(c, b, d); Z(b, b, d);
        S(d, e); S(f, a); M(a, c, a); M(c, b, e);
        A(e, a, c); Z(a, a, c); S(b, a); Z(c, d, f);
        M(a, c, C121665); A(a, a, d); M(c, c, a); M(a, d, f);
        M(d, b, x); S(b, e);
        sel25519(a, b, (int)r);
        sel25519(c, d, (int)r);
    }
    for (int i = 0; i < 16; i++) {
        x[i + 16] = a[i]; x[i + 32] = c[i]; x[i + 48] = b[i]; x[i + 64] = d[i];
    }
    inv25519(x + 32, x + 32);
    M(x + 16, x + 16, x + 32);
    pack25519(out, x + 16);
    crypto_wipe(z, sizeof z);
}

void x25519_base(u8 out[32], const u8 scalar[32])
{
    u8 nine[32] = {9};
    x25519(out, scalar, nine);
}

/* ---- Ed25519 ---- */
static void set25519(gf r, const gf a) { for (int i = 0; i < 16; i++) r[i] = a[i]; }

static void padd(gf p[4], gf q[4])
{
    gf a, b, c, d, t, e, f, g, h;
    Z(a, p[1], p[0]); Z(t, q[1], q[0]); M(a, a, t);
    A(b, p[0], p[1]); A(t, q[0], q[1]); M(b, b, t);
    M(c, p[3], q[3]); M(c, c, D2);
    M(d, p[2], q[2]); A(d, d, d);
    Z(e, b, a); Z(f, d, c); A(g, d, c); A(h, b, a);
    M(p[0], e, f); M(p[1], h, g); M(p[2], g, f); M(p[3], e, h);
}

static void cswap(gf p[4], gf q[4], u8 b)
{
    for (int i = 0; i < 4; i++) sel25519(p[i], q[i], b);
}

static void pack_point(u8 *r, gf p[4])
{
    gf tx, ty, zi;
    inv25519(zi, p[2]);
    M(tx, p[0], zi);
    M(ty, p[1], zi);
    pack25519(r, ty);
    r[31] ^= (u8)(par25519(tx) << 7);
}

static void pmult(gf p[4], gf q[4], const u8 *s)
{
    set25519(p[0], gf0); set25519(p[1], gf1); set25519(p[2], gf1); set25519(p[3], gf0);
    for (int i = 255; i >= 0; --i) {
        u8 b = (s[i / 8] >> (i & 7)) & 1;
        cswap(p, q, b);
        padd(q, p);
        padd(p, p);
        cswap(p, q, b);
    }
}

static void pbase(gf p[4], const u8 *s)
{
    gf q[4];
    set25519(q[0], GX); set25519(q[1], GY); set25519(q[2], gf1); M(q[3], GX, GY);
    pmult(p, q, s);
}

static void modL(u8 *r, i64 x[64])
{
    i64 carry;
    int i, j;
    for (i = 63; i >= 32; --i) {
        carry = 0;
        for (j = i - 32; j < i - 12; ++j) {
            x[j] += carry - 16 * x[i] * LL[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry << 8;
        }
        x[j] += carry;
        x[i] = 0;
    }
    carry = 0;
    for (j = 0; j < 32; j++) {
        x[j] += carry - (x[31] >> 4) * LL[j];
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (j = 0; j < 32; j++) x[j] -= carry * LL[j];
    for (i = 0; i < 32; i++) {
        x[i + 1] += x[i] >> 8;
        r[i] = (u8)(x[i] & 255);
    }
}

static void reduce64(u8 r[64])
{
    i64 x[64];
    for (int i = 0; i < 64; i++) x[i] = (i64)r[i];
    for (int i = 0; i < 64; i++) r[i] = 0;
    modL(r, x);
}

static void expand_seed(u8 d[64], const u8 seed[32])
{
    sha512(d, seed, 32);
    d[0] &= 248;
    d[31] &= 127;
    d[31] |= 64;
}

void ed25519_pubkey(u8 pk[32], const u8 seed[32])
{
    u8 d[64];
    gf p[4];
    expand_seed(d, seed);
    pbase(p, d);
    pack_point(pk, p);
    crypto_wipe(d, sizeof d);
}

void ed25519_sign(u8 sig[64], const u8 *msg, u32 n, const u8 seed[32])
{
    u8 d[64], h[64], r[64], pk[32];
    i64 x[64];
    gf p[4];
    sha512_ctx c;
    expand_seed(d, seed);
    pbase(p, d);
    pack_point(pk, p);
    sha512_init(&c); sha512_update(&c, d + 32, 32); sha512_update(&c, msg, n); sha512_final(&c, r);
    reduce64(r);
    pbase(p, r);
    pack_point(sig, p);                          /* R */
    sha512_init(&c); sha512_update(&c, sig, 32); sha512_update(&c, pk, 32); sha512_update(&c, msg, n);
    sha512_final(&c, h);
    reduce64(h);
    for (int i = 0; i < 64; i++) x[i] = 0;
    for (int i = 0; i < 32; i++) x[i] = (i64)r[i];
    for (int i = 0; i < 32; i++)
        for (int j = 0; j < 32; j++)
            x[i + j] += (i64)h[i] * (i64)d[j];
    modL(sig + 32, x);                           /* S = r + h*a mod L */
    crypto_wipe(d, sizeof d);
    crypto_wipe(r, sizeof r);
    crypto_wipe(x, sizeof x);
}

static int unpackneg(gf r[4], const u8 p[32])
{
    gf t, chk, num, den, den2, den4, den6;
    set25519(r[2], gf1);
    unpack25519(r[1], p);
    S(num, r[1]);
    M(den, num, D);
    Z(num, num, r[2]);
    A(den, r[2], den);
    S(den2, den); S(den4, den2); M(den6, den4, den2);
    M(t, den6, num); M(t, t, den);
    pow2523(t, t);
    M(t, t, num); M(t, t, den); M(t, t, den);
    M(r[0], t, den);
    S(chk, r[0]); M(chk, chk, den);
    if (neq25519(chk, num)) M(r[0], r[0], GI);
    S(chk, r[0]); M(chk, chk, den);
    if (neq25519(chk, num)) return -1;
    if (par25519(r[0]) == (p[31] >> 7)) Z(r[0], gf0, r[0]);
    M(r[3], r[0], r[1]);
    return 0;
}

int ed25519_verify(const u8 sig[64], const u8 *msg, u32 n, const u8 pk[32])
{
    u8 t[32], h[64];
    gf p[4], q[4];
    sha512_ctx c;
    /* S doit etre strictement inferieur a L (sinon la signature est malleable) */
    for (int i = 31; i >= 0; i--) {
        int s = sig[32 + i], l = (int)LL[i];
        if (s < l) break;
        if (s > l || i == 0) return -1;
    }
    if (unpackneg(q, pk))
        return -1;
    sha512_init(&c); sha512_update(&c, sig, 32); sha512_update(&c, pk, 32); sha512_update(&c, msg, n);
    sha512_final(&c, h);
    reduce64(h);
    pmult(p, q, h);
    pbase(q, sig + 32);
    padd(p, q);
    pack_point(t, p);
    return crypto_memeq(sig, t, 32) ? 0 : -1;
}
