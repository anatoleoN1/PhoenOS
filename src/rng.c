/* Generateur aleatoire : bassin SHA-256 + ChaCha20 a effacement de cle. Voir rng.h. */
#include "rng.h"
#include "crypto.h"
#include "string.h"

static sha256_ctx pool;
static int pool_ready_init;
static u32 credit;
static u8 key[32];
static u32 counter;
static int keyed;

static u32 read_tsc_low(void)
{
    u32 lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return lo ^ hi;
}

static void pool_start(void)
{
    if (!pool_ready_init) {
        sha256_init(&pool);
        pool_ready_init = 1;
    }
}

void rng_reset(void)
{
    pool_ready_init = 0;
    credit = 0;
    counter = 0;
    keyed = 0;
    crypto_wipe(key, sizeof key);
}

void rng_add(const void *data, u32 n, u32 credit_bits)
{
    pool_start();
    sha256_update(&pool, data, n);
    if (credit < 100000u)
        credit += credit_bits;
    keyed = 0;                                   /* la prochaine fois, on repart du bassin mis a jour */
}

void rng_event(u32 tag)
{
    u32 v[2];
    v[0] = tag;
    v[1] = read_tsc_low();
    rng_add(v, sizeof v, 1);
}

static int has_rdrand(void)
{
    u32 a = 1, b, c = 0, d;
    __asm__ volatile ("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
    return (c >> 30) & 1;
}

static int rdrand32(u32 *out)
{
    for (int tries = 0; tries < 10; tries++) {
        u8 ok;
        u32 v;
        __asm__ volatile ("rdrand %0; setc %1" : "=r"(v), "=qm"(ok) : : "cc");
        if (ok) {
            *out = v;
            return 1;
        }
    }
    return 0;
}

void rng_init(void)
{
    u32 prev = read_tsc_low(), last_d = 0;
    if (has_rdrand()) {
        u32 v[8];
        int n = 0;
        for (; n < 8; n++)
            if (!rdrand32(&v[n]))
                break;
        if (n == 8)
            rng_add(v, sizeof v, 256);
    }
    /* gigue : durees d'une boucle courte, mesurees au compteur de cycles ; on ne credite qu'un bit par mesure */
    for (int i = 0; i < 512; i++) {
        volatile u32 spin = 0;
        u32 now, d;
        for (int k = 0; k < 50 + (i & 7); k++)
            spin += (u32)k;
        now = read_tsc_low();
        d = now - prev;
        prev = now;
        rng_add(&d, sizeof d, d != last_d ? 1 : 0);     /* un delai qui ne varie jamais (machine trop reguliere) ne credite rien */
        last_d = d;
    }
}

int rng_ready(void)
{
    return credit >= 256;
}

static void rekey_from_pool(void)
{
    sha256_ctx copy = pool;
    u8 h[SHA256_LEN];
    sha256_final(&copy, h);
    memcpy(key, h, 32);
    crypto_wipe(h, sizeof h);
    /* le bassin continue de vivre : on y remet une trace du nouvel etat pour que deux amorcages different */
    sha256_update(&pool, key, 4);
    keyed = 1;
}

void rng_bytes(void *out, u32 n)
{
    u8 *o = out;
    u8 nonce[12], zero[64], next[64];
    pool_start();
    if (!keyed)
        rekey_from_pool();
    memset(zero, 0, sizeof zero);
    while (n) {
        u32 k = n < 32 ? n : 32;
        memset(nonce, 0, 12);
        nonce[0] = (u8)counter; nonce[1] = (u8)(counter >> 8); nonce[2] = (u8)(counter >> 16); nonce[3] = (u8)(counter >> 24);
        counter++;
        chacha20_xor(key, nonce, 0, zero, next, 64);
        memcpy(o, next, k);                      /* octets 0..31 : sortie */
        memcpy(key, next + 32, 32);              /* octets 32..63 : nouvelle cle (effacement de cle) */
        o += k; n -= k;
    }
    crypto_wipe(next, sizeof next);
    /* melange l'horloge a chaque appel : coute peu, aide si le bassin etait pauvre */
    {
        u32 t = read_tsc_low();
        sha256_update(&pool, &t, 4);
    }
}
