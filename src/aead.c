/* ChaCha20-Poly1305 (RFC 8439) et AES-GCM (NIST SP 800-38D). */
#include "crypto.h"
#include "string.h"

static u32 le32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static void put_le32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }
static void put_le64(u8 *p, u64 v) { put_le32(p, (u32)v); put_le32(p + 4, (u32)(v >> 32)); }
static u32 be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static void put_be32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }

/* ================= ChaCha20 ================= */
#define ROL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define QR(a, b, c, d) \
    a += b; d ^= a; d = ROL32(d, 16); \
    c += d; b ^= c; b = ROL32(b, 12); \
    a += b; d ^= a; d = ROL32(d, 8);  \
    c += d; b ^= c; b = ROL32(b, 7)

static void chacha_block(u8 out[64], const u8 key[32], u32 counter, const u8 nonce[12])
{
    u32 s[16], x[16];
    s[0] = 0x61707865u; s[1] = 0x3320646eu; s[2] = 0x79622d32u; s[3] = 0x6b206574u;
    for (int i = 0; i < 8; i++) s[4 + i] = le32(key + 4 * i);
    s[12] = counter;
    for (int i = 0; i < 3; i++) s[13 + i] = le32(nonce + 4 * i);
    memcpy(x, s, sizeof x);
    for (int r = 0; r < 10; r++) {
        QR(x[0], x[4], x[8],  x[12]); QR(x[1], x[5], x[9],  x[13]);
        QR(x[2], x[6], x[10], x[14]); QR(x[3], x[7], x[11], x[15]);
        QR(x[0], x[5], x[10], x[15]); QR(x[1], x[6], x[11], x[12]);
        QR(x[2], x[7], x[8],  x[13]); QR(x[3], x[4], x[9],  x[14]);
    }
    for (int i = 0; i < 16; i++) put_le32(out + 4 * i, x[i] + s[i]);
}

void chacha20_xor(const u8 key[32], const u8 nonce[12], u32 counter, const u8 *in, u8 *out, u32 n)
{
    u8 ks[64];
    while (n) {
        u32 k = n < 64 ? n : 64;
        chacha_block(ks, key, counter++, nonce);
        for (u32 i = 0; i < k; i++) out[i] = in[i] ^ ks[i];
        in += k; out += k; n -= k;
    }
    crypto_wipe(ks, sizeof ks);
}

/* ================= Poly1305 (5 morceaux de 26 bits) ================= */
typedef struct { u32 r[5], h[5], pad[4]; u8 buf[16]; u32 n; } poly_ctx;

static void poly_init(poly_ctx *p, const u8 key[32])
{
    p->r[0] = le32(key)      & 0x3ffffff;
    p->r[1] = (le32(key + 3) >> 2) & 0x3ffff03;
    p->r[2] = (le32(key + 6) >> 4) & 0x3ffc0ff;
    p->r[3] = (le32(key + 9) >> 6) & 0x3f03fff;
    p->r[4] = (le32(key + 12) >> 8) & 0x00fffff;
    for (int i = 0; i < 5; i++) p->h[i] = 0;
    for (int i = 0; i < 4; i++) p->pad[i] = le32(key + 16 + 4 * i);
    p->n = 0;
}

static void poly_block(poly_ctx *p, const u8 *m, u32 hibit)
{
    u32 r0 = p->r[0], r1 = p->r[1], r2 = p->r[2], r3 = p->r[3], r4 = p->r[4];
    u32 s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
    u32 h0 = p->h[0], h1 = p->h[1], h2 = p->h[2], h3 = p->h[3], h4 = p->h[4];
    u64 d0, d1, d2, d3, d4;
    u32 c;
    h0 += le32(m) & 0x3ffffff;
    h1 += (le32(m + 3) >> 2) & 0x3ffffff;
    h2 += (le32(m + 6) >> 4) & 0x3ffffff;
    h3 += (le32(m + 9) >> 6) & 0x3ffffff;
    h4 += (le32(m + 12) >> 8) | hibit;
    d0 = (u64)h0 * r0 + (u64)h1 * s4 + (u64)h2 * s3 + (u64)h3 * s2 + (u64)h4 * s1;
    d1 = (u64)h0 * r1 + (u64)h1 * r0 + (u64)h2 * s4 + (u64)h3 * s3 + (u64)h4 * s2;
    d2 = (u64)h0 * r2 + (u64)h1 * r1 + (u64)h2 * r0 + (u64)h3 * s4 + (u64)h4 * s3;
    d3 = (u64)h0 * r3 + (u64)h1 * r2 + (u64)h2 * r1 + (u64)h3 * r0 + (u64)h4 * s4;
    d4 = (u64)h0 * r4 + (u64)h1 * r3 + (u64)h2 * r2 + (u64)h3 * r1 + (u64)h4 * r0;
    c = (u32)(d0 >> 26); h0 = (u32)d0 & 0x3ffffff;
    d1 += c; c = (u32)(d1 >> 26); h1 = (u32)d1 & 0x3ffffff;
    d2 += c; c = (u32)(d2 >> 26); h2 = (u32)d2 & 0x3ffffff;
    d3 += c; c = (u32)(d3 >> 26); h3 = (u32)d3 & 0x3ffffff;
    d4 += c; c = (u32)(d4 >> 26); h4 = (u32)d4 & 0x3ffffff;
    h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff;
    h1 += c;
    p->h[0] = h0; p->h[1] = h1; p->h[2] = h2; p->h[3] = h3; p->h[4] = h4;
}

static void poly_update(poly_ctx *p, const u8 *d, u32 n)
{
    while (n) {
        u32 k = 16 - p->n;
        if (k > n) k = n;
        memcpy(p->buf + p->n, d, k);
        p->n += k; d += k; n -= k;
        if (p->n == 16) {
            poly_block(p, p->buf, 1u << 24);
            p->n = 0;
        }
    }
}

static void poly_final(poly_ctx *p, u8 tag[16])
{
    u32 h0, h1, h2, h3, h4, c, g0, g1, g2, g3, g4, mask;
    u64 f;
    if (p->n) {
        p->buf[p->n] = 1;
        for (u32 i = p->n + 1; i < 16; i++) p->buf[i] = 0;
        poly_block(p, p->buf, 0);
    }
    h0 = p->h[0]; h1 = p->h[1]; h2 = p->h[2]; h3 = p->h[3]; h4 = p->h[4];
    c = h1 >> 26; h1 &= 0x3ffffff; h2 += c;
    c = h2 >> 26; h2 &= 0x3ffffff; h3 += c;
    c = h3 >> 26; h3 &= 0x3ffffff; h4 += c;
    c = h4 >> 26; h4 &= 0x3ffffff; h0 += c * 5;
    c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;
    g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
    g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
    g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
    g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
    g4 = h4 + c - (1u << 26);
    mask = (g4 >> 31) - 1;                      /* tout a 1 si h >= p : on garde h - p */
    g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0; h1 = (h1 & mask) | g1; h2 = (h2 & mask) | g2;
    h3 = (h3 & mask) | g3; h4 = (h4 & mask) | g4;
    h0 = h0 | (h1 << 26);
    h1 = (h1 >> 6) | (h2 << 20);
    h2 = (h2 >> 12) | (h3 << 14);
    h3 = (h3 >> 18) | (h4 << 8);
    f = (u64)h0 + p->pad[0]; put_le32(tag, (u32)f);
    f = (u64)h1 + p->pad[1] + (f >> 32); put_le32(tag + 4, (u32)f);
    f = (u64)h2 + p->pad[2] + (f >> 32); put_le32(tag + 8, (u32)f);
    f = (u64)h3 + p->pad[3] + (f >> 32); put_le32(tag + 12, (u32)f);
    crypto_wipe(p, sizeof *p);
}

void poly1305_mac(u8 tag[16], const u8 key[32], const u8 *msg, u32 n)
{
    poly_ctx p;
    poly_init(&p, key);
    poly_update(&p, msg, n);
    poly_final(&p, tag);
}

static void poly_pad16(poly_ctx *p, u32 len)
{
    static const u8 z[16];
    if (len % 16)
        poly_update(p, z, 16 - len % 16);
}

static void cc_tag(u8 tag[16], const u8 key[32], const u8 nonce[12], const u8 *aad, u32 alen, const u8 *ct, u32 n)
{
    u8 otk[64], lens[16];
    poly_ctx p;
    chacha_block(otk, key, 0, nonce);
    poly_init(&p, otk);
    poly_update(&p, aad, alen);
    poly_pad16(&p, alen);
    poly_update(&p, ct, n);
    poly_pad16(&p, n);
    put_le64(lens, alen);
    put_le64(lens + 8, n);
    poly_update(&p, lens, 16);
    poly_final(&p, tag);
    crypto_wipe(otk, sizeof otk);
}

void chacha20poly1305_seal(const u8 key[32], const u8 nonce[12], const u8 *aad, u32 alen,
                           const u8 *in, u32 n, u8 *out, u8 tag[16])
{
    chacha20_xor(key, nonce, 1, in, out, n);
    cc_tag(tag, key, nonce, aad, alen, out, n);
}

int chacha20poly1305_open(const u8 key[32], const u8 nonce[12], const u8 *aad, u32 alen,
                          const u8 *in, u32 n, u8 *out, const u8 tag[16])
{
    u8 t[16];
    cc_tag(t, key, nonce, aad, alen, in, n);
    if (!crypto_memeq(t, tag, 16))
        return -1;
    chacha20_xor(key, nonce, 1, in, out, n);
    return 0;
}

/* ================= AES ================= */
static u8 sbox[256];
static int sbox_ready;

static void sbox_build(void)
{
    u8 p = 1, q = 1;
    do {                                         /* p parcourt les puissances de 3, q est son inverse */
        p = (u8)(p ^ (p << 1) ^ ((p & 0x80) ? 0x1b : 0));
        q ^= (u8)(q << 1); q ^= (u8)(q << 2); q ^= (u8)(q << 4);
        if (q & 0x80) q ^= 0x09;
        sbox[p] = (u8)(q ^ ((q << 1) | (q >> 7)) ^ ((q << 2) | (q >> 6)) ^ ((q << 3) | (q >> 5)) ^ ((q << 4) | (q >> 4)) ^ 0x63);
    } while (p != 1);
    sbox[0] = 0x63;
    sbox_ready = 1;
}

static u8 xt(u8 x) { return (u8)((x << 1) ^ ((x & 0x80) ? 0x1b : 0)); }

static void aes_encrypt(const aesgcm_ctx *c, const u8 in[16], u8 out[16])
{
    u8 s[16], t[16];
    for (int i = 0; i < 16; i++) s[i] = in[i] ^ c->rk[i];
    for (int r = 1; r <= c->rounds; r++) {
        for (int i = 0; i < 16; i++) t[i] = sbox[s[i]];
        /* ShiftRows : l'octet (ligne i, colonne j) vient de la colonne j+i */
        for (int col = 0; col < 4; col++)
            for (int row = 0; row < 4; row++)
                s[4 * col + row] = t[4 * ((col + row) & 3) + row];
        if (r != c->rounds) {
            for (int col = 0; col < 4; col++) {
                u8 *a = s + 4 * col;
                u8 a0 = a[0], a1 = a[1], a2 = a[2], a3 = a[3];
                a[0] = (u8)(xt(a0) ^ (xt(a1) ^ a1) ^ a2 ^ a3);
                a[1] = (u8)(a0 ^ xt(a1) ^ (xt(a2) ^ a2) ^ a3);
                a[2] = (u8)(a0 ^ a1 ^ xt(a2) ^ (xt(a3) ^ a3));
                a[3] = (u8)((xt(a0) ^ a0) ^ a1 ^ a2 ^ xt(a3));
            }
        }
        for (int i = 0; i < 16; i++) s[i] ^= c->rk[16 * r + i];
    }
    memcpy(out, s, 16);
}

/* ================= GCM ================= */
/* Produit dans GF(2^128), bit a bit (lent mais simple et sans table dependant des donnees). */
static void gmul(u32 x[4], const u32 h[4])
{
    u32 z[4] = {0, 0, 0, 0}, v[4];
    memcpy(v, h, sizeof v);
    for (int i = 0; i < 128; i++) {
        u32 bit = (x[i >> 5] >> (31 - (i & 31))) & 1;
        u32 m = (u32)0 - bit, lsb;
        for (int k = 0; k < 4; k++) z[k] ^= v[k] & m;
        lsb = v[3] & 1;
        v[3] = (v[3] >> 1) | (v[2] << 31);
        v[2] = (v[2] >> 1) | (v[1] << 31);
        v[1] = (v[1] >> 1) | (v[0] << 31);
        v[0] = (v[0] >> 1) ^ (0xe1000000u & ((u32)0 - lsb));
    }
    memcpy(x, z, sizeof z);
}

static void ghash_feed(u32 y[4], const u32 h[4], const u8 *d, u32 n)
{
    while (n) {
        u8 blk[16];
        u32 k = n < 16 ? n : 16;
        memset(blk, 0, 16);
        memcpy(blk, d, k);
        for (int i = 0; i < 4; i++) y[i] ^= be32(blk + 4 * i);
        gmul(y, h);
        d += k; n -= k;
    }
}

int aesgcm_init(aesgcm_ctx *c, const u8 *key, u32 klen)
{
    u32 nk, total;
    u8 rcon = 1, z[16];
    if (klen != 16 && klen != 32)
        return -1;
    if (!sbox_ready)
        sbox_build();
    nk = klen / 4;
    c->rounds = nk + 6;
    total = 16 * (c->rounds + 1);
    memcpy(c->rk, key, klen);
    for (u32 i = klen; i < total; i += 4) {
        u8 t[4];
        memcpy(t, c->rk + i - 4, 4);
        if ((i / 4) % nk == 0) {
            u8 x = t[0];
            t[0] = (u8)(sbox[t[1]] ^ rcon); t[1] = sbox[t[2]]; t[2] = sbox[t[3]]; t[3] = sbox[x];
            rcon = xt(rcon);
        } else if (nk > 6 && (i / 4) % nk == 4) {
            for (int j = 0; j < 4; j++) t[j] = sbox[t[j]];
        }
        for (int j = 0; j < 4; j++) c->rk[i + j] = c->rk[i + j - klen] ^ t[j];
    }
    memset(z, 0, 16);
    aes_encrypt(c, z, c->h);
    return 0;
}

static void gcm_ctr(const aesgcm_ctx *c, const u8 nonce[12], const u8 *in, u8 *out, u32 n)
{
    u8 cb[16], ks[16];
    u32 ctr = 2;
    memcpy(cb, nonce, 12);
    while (n) {
        u32 k = n < 16 ? n : 16;
        put_be32(cb + 12, ctr++);
        aes_encrypt(c, cb, ks);
        for (u32 i = 0; i < k; i++) out[i] = in[i] ^ ks[i];
        in += k; out += k; n -= k;
    }
}

static void gcm_tag(const aesgcm_ctx *c, const u8 nonce[12], const u8 *aad, u32 alen, const u8 *ct, u32 n, u8 tag[16])
{
    u32 h[4], y[4] = {0, 0, 0, 0};
    u8 j0[16], ek[16], lens[16];
    for (int i = 0; i < 4; i++) h[i] = be32(c->h + 4 * i);
    ghash_feed(y, h, aad, alen);
    ghash_feed(y, h, ct, n);
    put_be32(lens, 0); put_be32(lens + 4, alen * 8);
    put_be32(lens + 8, 0); put_be32(lens + 12, n * 8);
    /* alen et n sont < 2^29 en pratique (enregistrements TLS) : le haut des deux longueurs est nul */
    ghash_feed(y, h, lens, 16);
    memcpy(j0, nonce, 12);
    put_be32(j0 + 12, 1);
    aes_encrypt(c, j0, ek);
    for (int i = 0; i < 4; i++) put_be32(tag + 4 * i, y[i]);
    for (int i = 0; i < 16; i++) tag[i] ^= ek[i];
}

void aesgcm_seal(const aesgcm_ctx *c, const u8 nonce[12], const u8 *aad, u32 alen,
                 const u8 *in, u32 n, u8 *out, u8 tag[16])
{
    gcm_ctr(c, nonce, in, out, n);
    gcm_tag(c, nonce, aad, alen, out, n, tag);
}

int aesgcm_open(const aesgcm_ctx *c, const u8 nonce[12], const u8 *aad, u32 alen,
                const u8 *in, u32 n, u8 *out, const u8 tag[16])
{
    u8 t[16];
    gcm_tag(c, nonce, aad, alen, in, n, t);
    if (!crypto_memeq(t, tag, 16))
        return -1;
    gcm_ctr(c, nonce, in, out, n);
    return 0;
}
