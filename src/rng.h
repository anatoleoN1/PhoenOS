#ifndef PHOENOS_RNG_H
#define PHOENOS_RNG_H

#include "types.h"

/*
 * Generateur de nombres aleatoires (0.10.2) : de quoi tirer des cles TLS.
 * Un "bassin" d'entropie (SHA-256) nourrit un generateur ChaCha20 qui change de cle apres chaque tirage
 * (une cle volee plus tard ne revele pas les tirages passes).
 * Sources : RDRAND si le processeur l'a, gigue du compteur de cycles (rdtsc), et les evenements du systeme
 * (arrivee de paquets, touches du clavier) signales avec rng_event(). Limite honnete : sous une machine virtuelle
 * ou sans RDRAND, la qualite de la gigue est difficile a garantir : rng_ready() dit si on a assez de "credit".
 */

/* Ajoute des octets au bassin (credit_bits = entropie estimee, souvent bien moins que 8 par octet). */
void rng_add(const void *data, u32 n, u32 credit_bits);

/* Signale un evenement imprevisible (le moment exact est ajoute au bassin ; credit : 1 bit). */
void rng_event(u32 tag);

/* Collecte de depart : RDRAND (si present) et gigue du compteur de cycles. A appeler au demarrage. */
void rng_init(void);

/* Vrai quand le credit atteint 256 bits : on peut alors tirer des cles sans inquietude. */
int  rng_ready(void);

/* Remplit out de n octets aleatoires (ne bloque jamais ; verifier rng_ready() avant d'en faire des cles). */
void rng_bytes(void *out, u32 n);

/* Pour les tests : remise a zero. */
void rng_reset(void);

#endif
