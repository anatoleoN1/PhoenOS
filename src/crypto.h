#ifndef PHOENOS_CRYPTO_H
#define PHOENOS_CRYPTO_H

#include "types.h"

/*
 * Briques de chiffrement de PhoenOS (0.10.1) : la base de TLS 1.3 et des mises a jour signees.
 * Tout est ecrit sans allocation, sans division 64 bits (le noyau n'a pas libgcc) et teste contre des
 * vecteurs generes par la bibliotheque Python "cryptography" (tests/gen_crypto_vectors.py).
 *
 * Limite connue : les tables AES (S-box) sont lues par indice, donc pas a temps constant face a un
 * attaquant qui partagerait le meme processeur. Sans objet pour un noyau mono-utilisateur, a revoir plus tard.
 */

/* Comparaison a temps constant : 1 si egaux, 0 sinon. */
int crypto_memeq(const void *a, const void *b, u32 n);
/* Efface sans que le compilateur puisse l'optimiser. */
void crypto_wipe(void *p, u32 n);

/* ---- SHA-2 ---- */
#define SHA256_LEN 32
#define SHA384_LEN 48
#define SHA512_LEN 64

typedef struct { u32 h[8]; u8 buf[64];  u64 len; u32 n; } sha256_ctx;
typedef struct { u64 h[8]; u8 buf[128]; u64 len; u32 n; } sha512_ctx;   /* sert aussi a SHA-384 */

void sha256_init(sha256_ctx *c);
void sha256_update(sha256_ctx *c, const void *d, u32 n);
void sha256_final(sha256_ctx *c, u8 out[SHA256_LEN]);
void sha256(u8 out[SHA256_LEN], const void *d, u32 n);

void sha512_init(sha512_ctx *c);
void sha384_init(sha512_ctx *c);
void sha512_update(sha512_ctx *c, const void *d, u32 n);
void sha512_final(sha512_ctx *c, u8 out[SHA512_LEN]);
void sha384_final(sha512_ctx *c, u8 out[SHA384_LEN]);
void sha512(u8 out[SHA512_LEN], const void *d, u32 n);
void sha384(u8 out[SHA384_LEN], const void *d, u32 n);

/* ---- Hachage choisi a l'execution (TLS : SHA-256 ou SHA-384) ---- */
enum { HASH_SHA256 = 0, HASH_SHA384 = 1 };
typedef struct { int alg; union { sha256_ctx s256; sha512_ctx s512; } u; } hash_ctx;
#define HASH_MAX 48
u32  hash_len(int alg);                         /* 32 ou 48 ; 0 si alg inconnu */
u32  hash_block(int alg);                       /* 64 ou 128 */
void hash_init(hash_ctx *c, int alg);
void hash_update(hash_ctx *c, const void *d, u32 n);
void hash_final(hash_ctx *c, u8 *out);          /* ecrit hash_len(alg) octets */

/* HMAC (RFC 2104) en une fois. out = hash_len(alg) octets. */
void hmac(int alg, const u8 *key, u32 klen, const u8 *msg, u32 mlen, u8 *out);

/* HKDF (RFC 5869). expand : olen <= 255 * hash_len(alg), sinon renvoie -1. */
void hkdf_extract(int alg, const u8 *salt, u32 slen, const u8 *ikm, u32 ilen, u8 *prk);
int  hkdf_expand(int alg, const u8 *prk, const u8 *info, u32 ilen, u8 *out, u32 olen);

/* Poly1305 seul (RFC 8439) : cle a usage unique de 32 octets. */
void poly1305_mac(u8 tag[16], const u8 key[32], const u8 *msg, u32 n);

/* ---- ChaCha20-Poly1305 (RFC 8439) : cle 32 octets, nonce 12, etiquette 16 ---- */
void chacha20_xor(const u8 key[32], const u8 nonce[12], u32 counter, const u8 *in, u8 *out, u32 n);
void chacha20poly1305_seal(const u8 key[32], const u8 nonce[12], const u8 *aad, u32 alen,
                           const u8 *in, u32 n, u8 *out, u8 tag[16]);
/* Renvoie 0 si l'etiquette est bonne (alors out contient le clair), -1 sinon (out n est pas touche). */
int  chacha20poly1305_open(const u8 key[32], const u8 nonce[12], const u8 *aad, u32 alen,
                           const u8 *in, u32 n, u8 *out, const u8 tag[16]);

/* ---- AES-GCM (NIST SP 800-38D) : cle 16 ou 32 octets, nonce 12, etiquette 16 ---- */
typedef struct { u8 rk[240]; int rounds; u8 h[16]; } aesgcm_ctx;
int  aesgcm_init(aesgcm_ctx *c, const u8 *key, u32 klen);       /* 0 ou -1 si klen invalide */
void aesgcm_seal(const aesgcm_ctx *c, const u8 nonce[12], const u8 *aad, u32 alen,
                 const u8 *in, u32 n, u8 *out, u8 tag[16]);
int  aesgcm_open(const aesgcm_ctx *c, const u8 nonce[12], const u8 *aad, u32 alen,
                 const u8 *in, u32 n, u8 *out, const u8 tag[16]);

/* ---- Courbe 25519 ---- */
void x25519(u8 out[32], const u8 scalar[32], const u8 point[32]);
void x25519_base(u8 out[32], const u8 scalar[32]);

/* ---- Ed25519 (RFC 8032) ---- */
void ed25519_pubkey(u8 pk[32], const u8 seed[32]);
void ed25519_sign(u8 sig[64], const u8 *msg, u32 n, const u8 seed[32]);
/* 0 = signature valide, -1 = refusee (y compris S >= L et cle publique invalide). */
int  ed25519_verify(const u8 sig[64], const u8 *msg, u32 n, const u8 pk[32]);

#endif
