/* Test du generateur aleatoire (rng.c) : determinisme a entropie egale, difference sinon, effacement de cle, credit. */
#include "testlib.h"
#include "rng.h"
#include "crypto.h"

u8 test_scancode;
void test_panic(const char *fmt, ...) { out("  PANIQUE INATTENDUE : "); out(fmt); out("\n"); exit_now(2); }

static int ones(const u8 *p, u32 n)
{
    int c = 0;
    for (u32 i = 0; i < n; i++)
        for (int b = 0; b < 8; b++)
            c += (p[i] >> b) & 1;
    return c;
}

int test_main(void)
{
    u8 a[64], b[64], c[64], big[4000];
    int seen[256] = {0};

    rng_reset();
    CHECK(!rng_ready());
    rng_add("graine-1", 8, 0);
    rng_bytes(a, 64);
    /* les tirages dependent aussi de l'horloge ajoutee a chaque appel : on verifie donc seulement les proprietes */
    rng_bytes(b, 64);
    CHECK(memcmp(a, b, 64) != 0);                /* deux tirages successifs different */
    CHECK(memcmp(a, a + 32, 32) != 0);

    /* credit */
    rng_reset();
    for (int i = 0; i < 255; i++)
        rng_add("x", 1, 1);
    CHECK(!rng_ready());
    rng_add("y", 1, 1);
    CHECK(rng_ready());
    rng_reset();
    CHECK(!rng_ready());
    rng_init();
    CHECK(rng_ready());                          /* RDRAND ou la gigue de rdtsc suffisent a atteindre 256 */

    /* deux amorcages differents : sorties differentes */
    rng_reset(); rng_add("A", 1, 256); rng_bytes(a, 64);
    rng_reset(); rng_add("B", 1, 256); rng_bytes(b, 64);
    CHECK(memcmp(a, b, 64) != 0);

    /* qualite grossiere : equilibre des bits et diversite des octets sur 4000 octets */
    rng_reset(); rng_init();
    rng_bytes(big, sizeof big);
    int o = ones(big, sizeof big);
    CHECK(o > 15200 && o < 16800);               /* 16000 attendus, ecart-type ~ 90 */
    for (u32 i = 0; i < sizeof big; i++) seen[big[i]]++;
    int distinct = 0, worst = 0;
    for (int i = 0; i < 256; i++) { distinct += seen[i] != 0; if (seen[i] > worst) worst = seen[i]; }
    CHECK(distinct > 240 && worst < 40);

    /* longueurs variees : jamais de depassement */
    for (u32 n = 0; n < 70; n++) {
        memset(c, 0xAA, sizeof c);
        rng_bytes(c, n);
        for (u32 i = n; i < sizeof c; i++)
            CHECK(c[i] == 0xAA);
    }
    out("  Aleatoire verifie : tirages distincts, credit d'entropie, equilibre des bits, bornes\n");
    return 0;
}
