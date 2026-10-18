/* SHA-256 / SHA-384 / SHA-512 (FIPS 180-4), HMAC (RFC 2104), HKDF (RFC 5869). */
#include "crypto.h"
#include "string.h"

int crypto_memeq(const void *a, const void *b, u32 n)
{
    const u8 *x = a, *y = b;
    u32 d = 0;
    for (u32 i = 0; i < n; i++)
        d |= (u32)(x[i] ^ y[i]);
    return d == 0;
}

void crypto_wipe(void *p, u32 n)
{
    volatile u8 *v = p;
    while (n--)
        *v++ = 0;
}

static const u32 K256[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

static const u64 K512[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

static const u32 H256_INIT[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
};

static const u64 H512_INIT[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
};

static const u64 H384_INIT[8] = {
    0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL, 0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
    0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL, 0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL,
};

#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define ROR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))

static u32 be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static void put_be32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static u64 be64(const u8 *p) { return (u64)be32(p) << 32 | be32(p + 4); }
static void put_be64(u8 *p, u64 v) { put_be32(p, (u32)(v >> 32)); put_be32(p + 4, (u32)v); }

/* ---- SHA-256 ---- */
static void sha256_block(u32 h[8], const u8 *p)
{
    u32 w[64], a, b, c, d, e, f, g, hh;
    for (int i = 0; i < 16; i++)
        w[i] = be32(p + 4 * i);
    for (int i = 16; i < 64; i++) {
        u32 s0 = ROR32(w[i - 15], 7) ^ ROR32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        u32 s1 = ROR32(w[i - 2], 17) ^ ROR32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4]; f = h[5]; g = h[6]; hh = h[7];
    for (int i = 0; i < 64; i++) {
        u32 S1 = ROR32(e, 6) ^ ROR32(e, 11) ^ ROR32(e, 25);
        u32 ch = (e & f) ^ (~e & g);
        u32 t1 = hh + S1 + ch + K256[i] + w[i];
        u32 S0 = ROR32(a, 2) ^ ROR32(a, 13) ^ ROR32(a, 22);
        u32 mj = (a & b) ^ (a & c) ^ (b & c);
        u32 t2 = S0 + mj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

void sha256_init(sha256_ctx *c)
{
    memcpy(c->h, H256_INIT, sizeof c->h);
    c->len = 0;
    c->n = 0;
}

void sha256_update(sha256_ctx *c, const void *data, u32 n)
{
    const u8 *d = data;
    c->len += n;
    while (n) {
        u32 k = 64 - c->n;
        if (k > n) k = n;
        memcpy(c->buf + c->n, d, k);
        c->n += k; d += k; n -= k;
        if (c->n == 64) {
            sha256_block(c->h, c->buf);
            c->n = 0;
        }
    }
}

void sha256_final(sha256_ctx *c, u8 out[SHA256_LEN])
{
    u64 bits = c->len * 8;
    u8 pad = 0x80, z = 0, lenb[8];
    sha256_update(c, &pad, 1);
    while (c->n != 56)
        sha256_update(c, &z, 1);
    put_be64(lenb, bits);
    sha256_update(c, lenb, 8);
    for (int i = 0; i < 8; i++)
        put_be32(out + 4 * i, c->h[i]);
    crypto_wipe(c, sizeof *c);
}

void sha256(u8 out[SHA256_LEN], const void *d, u32 n)
{
    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, d, n);
    sha256_final(&c, out);
}

/* ---- SHA-512 / SHA-384 ---- */
static void sha512_block(u64 h[8], const u8 *p)
{
    u64 w[80], a, b, c, d, e, f, g, hh;
    for (int i = 0; i < 16; i++)
        w[i] = be64(p + 8 * i);
    for (int i = 16; i < 80; i++) {
        u64 s0 = ROR64(w[i - 15], 1) ^ ROR64(w[i - 15], 8) ^ (w[i - 15] >> 7);
        u64 s1 = ROR64(w[i - 2], 19) ^ ROR64(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4]; f = h[5]; g = h[6]; hh = h[7];
    for (int i = 0; i < 80; i++) {
        u64 S1 = ROR64(e, 14) ^ ROR64(e, 18) ^ ROR64(e, 41);
        u64 ch = (e & f) ^ (~e & g);
        u64 t1 = hh + S1 + ch + K512[i] + w[i];
        u64 S0 = ROR64(a, 28) ^ ROR64(a, 34) ^ ROR64(a, 39);
        u64 mj = (a & b) ^ (a & c) ^ (b & c);
        u64 t2 = S0 + mj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

void sha512_init(sha512_ctx *c) { memcpy(c->h, H512_INIT, sizeof c->h); c->len = 0; c->n = 0; }
void sha384_init(sha512_ctx *c) { memcpy(c->h, H384_INIT, sizeof c->h); c->len = 0; c->n = 0; }

void sha512_update(sha512_ctx *c, const void *data, u32 n)
{
    const u8 *d = data;
    c->len += n;
    while (n) {
        u32 k = 128 - c->n;
        if (k > n) k = n;
        memcpy(c->buf + c->n, d, k);
        c->n += k; d += k; n -= k;
        if (c->n == 128) {
            sha512_block(c->h, c->buf);
            c->n = 0;
        }
    }
}

static void sha512_finish(sha512_ctx *c, u8 *out, int words)
{
    u64 bits = c->len * 8;
    u8 pad = 0x80, z = 0, lenb[16];
    sha512_update(c, &pad, 1);
    while (c->n != 112)
        sha512_update(c, &z, 1);
    for (int i = 0; i < 8; i++) lenb[i] = 0;          /* longueur sur 128 bits : le haut reste nul */
    put_be64(lenb + 8, bits);
    sha512_update(c, lenb, 16);
    for (int i = 0; i < words; i++)
        put_be64(out + 8 * i, c->h[i]);
    crypto_wipe(c, sizeof *c);
}

void sha512_final(sha512_ctx *c, u8 out[SHA512_LEN]) { sha512_finish(c, out, 8); }
void sha384_final(sha512_ctx *c, u8 out[SHA384_LEN]) { sha512_finish(c, out, 6); }

void sha512(u8 out[SHA512_LEN], const void *d, u32 n)
{
    sha512_ctx c;
    sha512_init(&c);
    sha512_update(&c, d, n);
    sha512_final(&c, out);
}

void sha384(u8 out[SHA384_LEN], const void *d, u32 n)
{
    sha512_ctx c;
    sha384_init(&c);
    sha512_update(&c, d, n);
    sha384_final(&c, out);
}

/* ---- Hachage a choisir a l'execution ---- */
u32 hash_len(int alg)   { return alg == HASH_SHA256 ? 32 : alg == HASH_SHA384 ? 48 : 0; }
u32 hash_block(int alg) { return alg == HASH_SHA256 ? 64 : alg == HASH_SHA384 ? 128 : 0; }

void hash_init(hash_ctx *c, int alg)
{
    c->alg = alg;
    if (alg == HASH_SHA256) sha256_init(&c->u.s256);
    else                    sha384_init(&c->u.s512);
}

void hash_update(hash_ctx *c, const void *d, u32 n)
{
    if (c->alg == HASH_SHA256) sha256_update(&c->u.s256, d, n);
    else                       sha512_update(&c->u.s512, d, n);
}

void hash_final(hash_ctx *c, u8 *out)
{
    if (c->alg == HASH_SHA256) sha256_final(&c->u.s256, out);
    else                       sha384_final(&c->u.s512, out);
}

/* ---- HMAC ---- */
void hmac(int alg, const u8 *key, u32 klen, const u8 *msg, u32 mlen, u8 *out)
{
    u8 k[128], tmp[HASH_MAX];
    u32 bl = hash_block(alg), hl = hash_len(alg);
    hash_ctx c;
    if (!bl)
        return;
    memset(k, 0, sizeof k);
    if (klen > bl) {
        hash_init(&c, alg); hash_update(&c, key, klen); hash_final(&c, k);
    } else if (klen) {
        memcpy(k, key, klen);
    }
    for (u32 i = 0; i < bl; i++) k[i] ^= 0x36;
    hash_init(&c, alg); hash_update(&c, k, bl); hash_update(&c, msg, mlen); hash_final(&c, tmp);
    for (u32 i = 0; i < bl; i++) k[i] ^= 0x36 ^ 0x5c;
    hash_init(&c, alg); hash_update(&c, k, bl); hash_update(&c, tmp, hl); hash_final(&c, out);
    crypto_wipe(k, sizeof k);
    crypto_wipe(tmp, sizeof tmp);
}

/* ---- HKDF ---- */
void hkdf_extract(int alg, const u8 *salt, u32 slen, const u8 *ikm, u32 ilen, u8 *prk)
{
    u8 zero[HASH_MAX];
    if (!salt || !slen) {                       /* sel absent : HashLen octets a zero */
        memset(zero, 0, sizeof zero);
        salt = zero;
        slen = hash_len(alg);
    }
    hmac(alg, salt, slen, ikm, ilen, prk);
}

int hkdf_expand(int alg, const u8 *prk, const u8 *info, u32 ilen, u8 *out, u32 olen)
{
    u32 hl = hash_len(alg), bl = hash_block(alg), done = 0;
    u8 t[HASH_MAX], k[128];
    u8 counter = 0;
    hash_ctx c;
    if (!hl || olen > 255u * hl)
        return -1;
    memset(k, 0, sizeof k);
    memcpy(k, prk, hl);                         /* prk fait hash_len octets : jamais plus que le bloc */
    while (done < olen) {
        u8 ik[128], ok[128], inner[HASH_MAX];
        u32 take = olen - done < hl ? olen - done : hl;
        counter++;
        for (u32 i = 0; i < bl; i++) { ik[i] = k[i] ^ 0x36; ok[i] = k[i] ^ 0x5c; }
        hash_init(&c, alg);
        hash_update(&c, ik, bl);
        if (counter > 1) hash_update(&c, t, hl);
        hash_update(&c, info, ilen);
        hash_update(&c, &counter, 1);
        hash_final(&c, inner);
        hash_init(&c, alg);
        hash_update(&c, ok, bl);
        hash_update(&c, inner, hl);
        hash_final(&c, t);
        memcpy(out + done, t, take);
        done += take;
    }
    crypto_wipe(k, sizeof k);
    crypto_wipe(t, sizeof t);
    return 0;
}
