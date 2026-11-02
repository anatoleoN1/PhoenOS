/*
 * Test des certificats X.509 (x509.c) : une petite PKI produite par Python (tests/gen_x509_vectors.py) et des scenarios
 * (chaine complete, intermediaire manquant, nom, joker, dates, autorite, longueur de chemin, extensions, signatures
 * RSA/ECDSA/Ed25519, alteration...), puis lecture et auto-signature des vraies racines embarquees (roots.c).
 */
#include "testlib.h"
#include "x509.h"

u8 test_scancode;
void test_panic(const char *fmt, ...) { out("  PANIQUE INATTENDUE : "); out(fmt); out("\n"); exit_now(2); }

#include "x509_vectors.h"
#define LE_IDX 2

extern const u8 *const roots_der[];
extern const u32 roots_len[];
extern const int roots_count;

static u8 store[64][5000];
static u32 slen[64];
#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

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

int test_main(void)
{
    const u8 *ptr[64];
    int ok_sc = 0;

    /* dates */
    CHECK(x509_time_from_civil(2000, 1, 1, 0, 0, 0) == 946684800u);
    CHECK(x509_time_from_civil(2024, 2, 29, 0, 0, 0) == 1709164800u);
    CHECK(x509_time_from_civil(2038, 1, 19, 3, 14, 7) == 2147483647u);
    CHECK(x509_time_from_civil(2023, 2, 29, 0, 0, 0) == 0);
    CHECK(x509_time_from_civil(2026, 13, 1, 0, 0, 0) == 0);
    CHECK(x509_time_from_civil(2200, 1, 1, 0, 0, 0) == 0xffffffffu);

    for (int i = 0; i < N(X_CERT); i++) {
        slen[i] = (u32)unhex(store[i], X_CERT[i]);
        ptr[i] = store[i];
    }
    for (int i = 0; i < N(V_CHAIN); i++) {
        const struct v_chain *v = &V_CHAIN[i];
        const u8 *ch[5], *rt[3];
        u32 cl[5], rl[3];
        int r;
        for (int k = 0; k < v->n; k++) { ch[k] = ptr[v->idx[k]]; cl[k] = slen[v->idx[k]]; }
        for (int k = 0; k < v->nroots; k++) { rt[k] = ptr[v->roots[k]]; rl[k] = slen[v->roots[k]]; }
        x509_set_roots(rt, rl, v->nroots);
        r = x509_verify_chain(ch, cl, v->n, v->host, v->now);
        if (r != v->want) {
            out("  scenario \""); out(v->name); out("\" : obtenu "); outnum((u32)(-r)); out(" (-), attendu "); outnum((u32)(-v->want)); out(" (-)\n");
            test_fails++;
        } else ok_sc++;
    }
    x509_set_roots(NULL, NULL, 0);

    /* vraies racines embarquees : lecture + signature par elle-meme */
    {
        int parsed = 0, selfsig = 0;
        CHECK(roots_count >= 30);
        for (int i = 0; i < roots_count; i++) {
            struct x509 c;
            int r = x509_parse(&c, roots_der[i], roots_len[i]);
            if (r == X509_OK) {
                parsed++;
                CHECK(c.has_bc && c.is_ca);
                CHECK(c.issuerlen == c.subjectlen);
                if (x509_check_sig(&c, &c) == X509_OK) selfsig++;
                else { out("  racine "); outnum((u32)i); out(" : auto-signature non verifiee\n"); }
            } else { out("  racine "); outnum((u32)i); out(" illisible, code "); outnum((u32)(-r)); out("\n"); }
        }
        CHECK(parsed == roots_count);
        CHECK(selfsig == roots_count);
        out("  racines embarquees : "); outnum((u32)parsed); out(" lues, "); outnum((u32)selfsig); out(" auto-signatures verifiees\n");
    }
    /* entrees hostiles : DER tronque a chaque longueur ne doit jamais planter */
    {
        struct x509 c;
        for (u32 n = 0; n < slen[LE_IDX]; n += 7)
            (void)x509_parse(&c, store[LE_IDX], n);
        CHECK(x509_parse(&c, store[LE_IDX], slen[LE_IDX]) == X509_OK);
        u8 junk[300];
        for (int i = 0; i < 300; i++) junk[i] = (u8)(i * 91 + 7);
        CHECK(x509_parse(&c, junk, 300) != X509_OK);
        junk[0] = 0x30; junk[1] = 0x82; junk[2] = 0xff; junk[3] = 0xff;
        CHECK(x509_parse(&c, junk, 300) != X509_OK);
    }
    out("  X.509 verifie : "); outnum((u32)ok_sc); out(" scenarios de chaine, noms, dates, autorites, extensions, signatures, alterations\n");
    return 0;
}
