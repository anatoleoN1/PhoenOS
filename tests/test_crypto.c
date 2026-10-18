/*
 * Test des briques de chiffrement (crypto.h) contre des vecteurs de reference generes par la bibliotheque
 * Python "cryptography" (tests/gen_crypto_vectors.py) : SHA-2, HMAC, HKDF, ChaCha20-Poly1305, AES-GCM,
 * X25519, Ed25519, plus des attaques (etiquette, message ou signature alteres).
 */
#include "testlib.h"
#include "crypto.h"

u8 test_scancode;
void test_panic(const char *fmt, ...) { out("  PANIQUE INATTENDUE : "); out(fmt); out("\n"); exit_now(2); }

#include "crypto_vectors.h"

static void pat(u8 *o, u32 n, u32 salt)
{
    for (u32 i = 0; i < n; i++)
        o[i] = (u8)(i * 131 + salt * 17 + (i >> 3) * 7 + 5);
}

static int unhex(u8 *o, const char *h)
{
    int n = 0;
    while (h[0] && h[1]) {
        u8 v[2];
        for (int i = 0; i < 2; i++) {
            char c = h[i];
            v[i] = (u8)(c <= '9' ? c - '0' : c - 'a' + 10);
        }
        o[n++] = (u8)(v[0] << 4 | v[1]);
        h += 2;
    }
    return n;
}

static u8 A[13000], B[13000], C[13000], D[13000];
#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

static void test_hash(void)
{
    for (int i = 0; i < N(V_HASH); i++) {
        const struct v_hash *v = &V_HASH[i];
        u8 got[64], want[64];
        pat(A, v->len, 1);
        sha256(got, A, v->len); unhex(want, v->s256); CHECK(memcmp(got, want, 32) == 0);
        sha384(got, A, v->len); unhex(want, v->s384); CHECK(memcmp(got, want, 48) == 0);
        sha512(got, A, v->len); unhex(want, v->s512); CHECK(memcmp(got, want, 64) == 0);
        /* en plusieurs morceaux de tailles variees : meme resultat */
        sha256_ctx c;
        sha256_init(&c);
        for (u32 off = 0, step = 1; off < v->len; off += step, step = step * 3 % 70 + 1)
            sha256_update(&c, A + off, off + step > v->len ? v->len - off : step);
        sha256_final(&c, got); unhex(want, v->s256); CHECK(memcmp(got, want, 32) == 0);
        sha512_ctx d;
        sha512_init(&d);
        for (u32 off = 0, step = 5; off < v->len; off += step, step = step * 7 % 150 + 1)
            sha512_update(&d, A + off, off + step > v->len ? v->len - off : step);
        sha512_final(&d, got); unhex(want, v->s512); CHECK(memcmp(got, want, 64) == 0);
    }
}

static void test_hmac_hkdf(void)
{
    for (int i = 0; i < N(V_HMAC); i++) {
        const struct v_hmac *v = &V_HMAC[i];
        u8 got[48], want[48];
        pat(A, v->klen, 2); pat(B, v->mlen, 3);
        hmac(v->alg, A, v->klen, B, v->mlen, got);
        unhex(want, v->mac);
        CHECK(memcmp(got, want, hash_len(v->alg)) == 0);
    }
    for (int i = 0; i < N(V_HKDF); i++) {
        const struct v_hkdf *v = &V_HKDF[i];
        u8 prk[48];
        pat(A, v->ilen, 4); pat(B, v->slen, 5); pat(C, v->ilen_info, 6);
        hkdf_extract(v->alg, v->slen ? B : NULL, v->slen, A, v->ilen, prk);
        CHECK(hkdf_expand(v->alg, prk, C, v->ilen_info, D, v->olen) == 0);
        unhex(A, v->okm);
        CHECK(memcmp(D, A, v->olen) == 0);
    }
    u8 prk[32] = {0};
    CHECK(hkdf_expand(HASH_SHA256, prk, NULL, 0, D, 255 * 32 + 1) == -1);   /* trop long */
    CHECK(hash_len(7) == 0);
}

static void test_aead(void)
{
    for (int i = 0; i < N(V_POLY); i++) {
        u8 key[32], tag[16], want[16];
        int ml = unhex(A, V_POLY[i].msg);
        unhex(key, V_POLY[i].key); unhex(want, V_POLY[i].tag);
        poly1305_mac(tag, key, A, (u32)ml);
        CHECK(memcmp(tag, want, 16) == 0);
    }
    u8 key[32], nonce[12], tag[16];
    for (int i = 0; i < N(V_CHACHA); i++) {
        const struct v_aead *v = &V_CHACHA[i];
        pat(nonce, 12, v->kseed + 1);
        pat(key, 32, v->kseed); pat(A, v->mlen, 9); pat(B, v->alen, 10);
        unhex(C, v->ct_tag);                           /* chiffre + etiquette */
        chacha20poly1305_seal(key, nonce, B, v->alen, A, v->mlen, D, tag);
        CHECK(memcmp(D, C, v->mlen) == 0 && memcmp(tag, C + v->mlen, 16) == 0);
        CHECK(chacha20poly1305_open(key, nonce, B, v->alen, C, v->mlen, D, C + v->mlen) == 0);
        CHECK(memcmp(D, A, v->mlen) == 0);
        /* alterations : etiquette, texte, donnees associees, nonce */
        u8 bad[16]; memcpy(bad, C + v->mlen, 16); bad[5] ^= 1;
        CHECK(chacha20poly1305_open(key, nonce, B, v->alen, C, v->mlen, D, bad) == -1);
        if (v->mlen) {
            C[v->mlen / 2] ^= 0x80;
            CHECK(chacha20poly1305_open(key, nonce, B, v->alen, C, v->mlen, D, C + v->mlen) == -1);
            C[v->mlen / 2] ^= 0x80;
        }
        if (v->alen)
            CHECK(chacha20poly1305_open(key, nonce, B, v->alen - 1, C, v->mlen, D, C + v->mlen) == -1);
        nonce[0] ^= 1;
        CHECK(chacha20poly1305_open(key, nonce, B, v->alen, C, v->mlen, D, C + v->mlen) == -1);
        nonce[0] ^= 1;
    }
    for (int i = 0; i < N(V_GCM); i++) {
        const struct v_aead *v = &V_GCM[i];
        aesgcm_ctx g;
        pat(nonce, 12, v->kseed + 1);
        pat(key, v->klen, v->kseed); pat(A, v->mlen, 12); pat(B, v->alen, 13);
        unhex(C, v->ct_tag);
        CHECK(aesgcm_init(&g, key, v->klen) == 0);
        aesgcm_seal(&g, nonce, B, v->alen, A, v->mlen, D, tag);
        CHECK(memcmp(D, C, v->mlen) == 0 && memcmp(tag, C + v->mlen, 16) == 0);
        CHECK(aesgcm_open(&g, nonce, B, v->alen, C, v->mlen, D, C + v->mlen) == 0);
        CHECK(memcmp(D, A, v->mlen) == 0);
        u8 bad[16]; memcpy(bad, C + v->mlen, 16); bad[15] ^= 0x01;
        CHECK(aesgcm_open(&g, nonce, B, v->alen, C, v->mlen, D, bad) == -1);
        if (v->mlen) {
            C[0] ^= 1;
            CHECK(aesgcm_open(&g, nonce, B, v->alen, C, v->mlen, D, C + v->mlen) == -1);
            C[0] ^= 1;
        }
    }
    aesgcm_ctx g;
    CHECK(aesgcm_init(&g, key, 24) == -1);               /* AES-192 volontairement absent */
}

static void test_curve(void)
{
    for (int i = 0; i < N(V_X25519); i++) {
        u8 sk[32], pt[32], want[32], got[32];
        unhex(sk, V_X25519[i].scalar); unhex(pt, V_X25519[i].point); unhex(want, V_X25519[i].out);
        x25519(got, sk, pt);
        CHECK(memcmp(got, want, 32) == 0);
    }
    for (int i = 0; i < N(V_X25519B); i++) {
        u8 sk[32], want[32], got[32];
        unhex(sk, V_X25519B[i].scalar); unhex(want, V_X25519B[i].pub);
        x25519_base(got, sk);
        CHECK(memcmp(got, want, 32) == 0);
    }
    /* Diffie-Hellman : les deux cotes trouvent le meme secret */
    {
        u8 a[32], b[32], pa[32], pb[32], s1[32], s2[32];
        pat(a, 32, 91); pat(b, 32, 92);
        x25519_base(pa, a); x25519_base(pb, b);
        x25519(s1, a, pb); x25519(s2, b, pa);
        CHECK(memcmp(s1, s2, 32) == 0);
    }
    for (int i = 0; i < N(V_ED); i++) {
        const struct v_ed *v = &V_ED[i];
        u8 seed[32], pk[32], sig[64], want[64], got[32];
        unhex(seed, V_ED[i].seed); unhex(pk, V_ED[i].pk); unhex(want, V_ED[i].sig);
        pat(A, v->mlen, 14);
        ed25519_pubkey(got, seed);
        CHECK(memcmp(got, pk, 32) == 0);
        ed25519_sign(sig, A, v->mlen, seed);
        CHECK(memcmp(sig, want, 64) == 0);
        CHECK(ed25519_verify(want, A, v->mlen, pk) == 0);
        /* attaques */
        sig[10] ^= 1;     CHECK(ed25519_verify(sig, A, v->mlen, pk) == -1); sig[10] ^= 1;
        sig[40] ^= 1;     CHECK(ed25519_verify(sig, A, v->mlen, pk) == -1); sig[40] ^= 1;
        if (v->mlen) {
            A[0] ^= 1;    CHECK(ed25519_verify(sig, A, v->mlen, pk) == -1); A[0] ^= 1;
        }
        pk[3] ^= 1;       CHECK(ed25519_verify(sig, A, v->mlen, pk) == -1);
        /* S + L est mathematiquement valide mais doit etre refuse (signature malleable) */
        {
            static const u8 L[32] = {237,211,245,92,26,99,18,88,214,156,247,162,222,249,222,20,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,16};
            u8 m[64]; unsigned carry = 0;
            memcpy(m, want, 64);
            for (int k = 0; k < 32; k++) { unsigned s = m[32 + k] + L[k] + carry; m[32 + k] = (u8)s; carry = s >> 8; }
            CHECK(ed25519_verify(m, A, v->mlen, got) == -1);
        }
    }
    {   /* S == L exactement : refusee */
        static const u8 L[32] = {237,211,245,92,26,99,18,88,214,156,247,162,222,249,222,20,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,16};
        u8 sig[64] = {0}, pk[32];
        memcpy(sig + 32, L, 32);
        unhex(pk, V_ED[0].pk);
        CHECK(ed25519_verify(sig, A, 0, pk) == -1);
    }
    {   /* cle publique qui n'est pas sur la courbe / signature toute a zero */
        u8 sig[64] = {0}, pk[32];
        pat(pk, 32, 3);
        CHECK(ed25519_verify(sig, A, 0, pk) == -1);
    }
}

int test_main(void)
{
    test_hash();
    test_hmac_hkdf();
    test_aead();
    test_curve();
    out("  Chiffrement verifie : SHA-256/384/512, HMAC, HKDF, ChaCha20-Poly1305, AES-128/256-GCM, X25519, Ed25519 + alterations\n");
    return 0;
}
