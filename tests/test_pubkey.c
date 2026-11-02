/*
 * Test de la verification de signatures (pubkey.c) : RSA PKCS#1 v1.5 et PSS (1024 a 4096 bits), ECDSA P-256 et P-384,
 * contre des signatures produites par la bibliotheque Python "cryptography" (tests/gen_pk_vectors.py), plus des attaques.
 */
#include "testlib.h"
#include "crypto.h"
#include "pubkey.h"

u8 test_scancode;
void test_panic(const char *fmt, ...) { out("  PANIQUE INATTENDUE : "); out(fmt); out("\n"); exit_now(2); }

#include "pk_vectors.h"

static int unhex(u8 *o, const char *h)
{
    int n = 0;
    while (h[0] && h[1]) {
        u8 v[2];
        for (int i = 0; i < 2; i++) { char c = h[i]; v[i] = (u8)(c <= '9' ? c - '0' : c - 'a' + 10); }
        o[n++] = (u8)(v[0] << 4 | v[1]);
        h += 2;
    }
    return n;
}

#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))
static u8 nb[600], eb[8], hb[64], sg[600], bad[600];

int test_main(void)
{
    int ok_rsa = 0, ok_ec = 0;
    for (int i = 0; i < N(V_RSA); i++) {
        const struct v_rsa *v = &V_RSA[i];
        u32 nl = (u32)unhex(nb, v->n), el = (u32)unhex(eb, v->e), sl = (u32)unhex(sg, v->sig);
        int (*fn)(const u8 *, u32, const u8 *, u32, int, const u8 *, const u8 *, u32) = v->pss ? rsa_verify_pss : rsa_verify_pkcs1;
        int (*other)(const u8 *, u32, const u8 *, u32, int, const u8 *, const u8 *, u32) = v->pss ? rsa_verify_pkcs1 : rsa_verify_pss;
        unhex(hb, v->hash);
        CHECK(fn(nb, nl, eb, el, v->hashalg, hb, sg, sl) == 0);
        ok_rsa++;
        CHECK(other(nb, nl, eb, el, v->hashalg, hb, sg, sl) == -1);          /* mauvais schema */
        CHECK(fn(nb, nl, eb, el, (v->hashalg + 1) % 3, hb, sg, sl) == -1);   /* mauvais hachage */
        memcpy(bad, sg, sl); bad[sl / 2] ^= 1;
        CHECK(fn(nb, nl, eb, el, v->hashalg, hb, bad, sl) == -1);            /* signature alteree */
        memcpy(bad, hb, 64); bad[3] ^= 0x10;
        CHECK(fn(nb, nl, eb, el, v->hashalg, bad, sg, sl) == -1);            /* message altere */
        CHECK(fn(nb, nl, eb, el, v->hashalg, hb, sg, sl - 1) == -1);         /* mauvaise longueur */
        memset(bad, 0xff, sl);
        CHECK(fn(nb, nl, eb, el, v->hashalg, hb, bad, sl) == -1);            /* signature >= n */
        memcpy(bad, nb, nl); bad[nl - 1] ^= 2;                                /* autre module */
        CHECK(fn(bad, nl, eb, el, v->hashalg, hb, sg, sl) == -1);
        CHECK(fn(nb, nl, eb, 0, v->hashalg, hb, sg, sl) == -1);              /* e absent */
    }
    for (int i = 0; i < N(V_RSA_BAD); i++) {
        const struct v_rsa *v = &V_RSA_BAD[i];
        u32 nl = (u32)unhex(nb, v->n), el = (u32)unhex(eb, v->e), sl = (u32)unhex(sg, v->sig);
        unhex(hb, v->hash);
        CHECK(rsa_verify_pkcs1(nb, nl, eb, el, v->hashalg, hb, sg, sl) == (i == 0 ? 0 : -1));   /* 0 = temoin valide */
    }
    for (int i = 0; i < N(V_EC); i++) {
        const struct v_ec *v = &V_EC[i];
        u32 pl = (u32)unhex(nb, v->pub), hl = (u32)unhex(hb, v->hash), sl = (u32)unhex(sg, v->sig);
        CHECK(ecdsa_verify_der(v->curve, nb, pl, hb, hl, sg, sl) == 0);
        ok_ec++;
        memcpy(bad, sg, sl); bad[sl - 5] ^= 1;
        CHECK(ecdsa_verify_der(v->curve, nb, pl, hb, hl, bad, sl) == -1);    /* s altere */
        memcpy(bad, sg, sl); bad[6] ^= 1;
        CHECK(ecdsa_verify_der(v->curve, nb, pl, hb, hl, bad, sl) == -1);    /* r altere */
        memcpy(bad, hb, 64); bad[1] ^= 1;
        CHECK(ecdsa_verify_der(v->curve, nb, pl, bad, hl, sg, sl) == -1);
        memcpy(bad, nb, pl); bad[pl - 1] ^= 1;                                /* point hors courbe */
        CHECK(ecdsa_verify_der(v->curve, bad, pl, hb, hl, sg, sl) == -1);
        CHECK(ecdsa_verify_der(3 - v->curve, nb, pl, hb, hl, sg, sl) == -1); /* mauvaise courbe */
        CHECK(ecdsa_verify_der(v->curve, nb, pl, hb, hl, sg, sl - 1) == -1); /* DER tronque */
        memcpy(bad, sg, sl); bad[0] = 0x31;
        CHECK(ecdsa_verify_der(v->curve, nb, pl, hb, hl, bad, sl) == -1);
        nb[0] = 2;                                                            /* format de point inconnu */
        CHECK(ecdsa_verify_der(v->curve, nb, pl, hb, hl, sg, sl) == -1);
    }
    {   /* r = 0 ou s = 0 */
        u8 z[8] = {0x30, 6, 2, 1, 0, 2, 1, 0};
        unhex(nb, V_EC[0].pub);
        CHECK(ecdsa_verify_der(1, nb, 65, hb, 32, z, 8) == -1);
    }
    out("  Signatures verifiees : "); outnum((u32)ok_rsa); out(" RSA (PKCS#1 et PSS, 1024-4096 bits), ");
    outnum((u32)ok_ec); out(" ECDSA (P-256, P-384), plus alterations\n");
    return 0;
}
