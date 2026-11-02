#ifndef PHOENOS_PUBKEY_H
#define PHOENOS_PUBKEY_H

#include "types.h"

/*
 * Verification de signatures a cle publique (0.10.2) : RSA (PKCS#1 v1.5 et PSS) et ECDSA (P-256, P-384).
 * Verification seulement (pas de signature) : tout est public, donc sans precaution de temps constant.
 * Chaque fonction renvoie 0 si la signature est valide, -1 sinon (y compris pour une entree mal formee).
 * hashalg : HASH_SHA256 / HASH_SHA384 / HASH_SHA512 (crypto.h) ; hash = condense du message deja calcule.
 */
enum { CURVE_P256 = 1, CURVE_P384 = 2 };

/* RSA : n et e en gros-boutiste, modules de 1024 a 4096 bits. */
int rsa_verify_pkcs1(const u8 *n, u32 nlen, const u8 *e, u32 elen, int hashalg, const u8 *hash, const u8 *sig, u32 siglen);
/* PSS avec MGF1 du meme hachage et sel de la taille du condense (exigence de TLS 1.3). */
int rsa_verify_pss(const u8 *n, u32 nlen, const u8 *e, u32 elen, int hashalg, const u8 *hash, const u8 *sig, u32 siglen);

/* ECDSA : pub = 0x04 || X || Y ; signature DER (SEQUENCE { INTEGER r, INTEGER s }). */
int ecdsa_verify_der(int curve, const u8 *pub, u32 publen, const u8 *hash, u32 hlen, const u8 *sig, u32 siglen);

#endif
