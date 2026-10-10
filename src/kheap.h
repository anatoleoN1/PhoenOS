#ifndef PHOENOS_KHEAP_H
#define PHOENOS_KHEAP_H

#include "types.h"

struct kheap_stats {
    u32 used_bytes;     /* octets dans les blocs alloués */
    u32 free_bytes;     /* octets dans les blocs libres  */
    u32 used_blocks;
    u32 free_blocks;
};

/* Réserve la mémoire du tas (jusqu'à 8 Mio) auprès du gestionnaire physique. */
void kheap_init(void);

/* Taille totale du tas en octets. */
u32  kheap_size(void);

/*
 * Alloue `size` octets (alignés sur 16). Renvoie NULL si size == 0 ou si
 * le tas est plein : l'appelant DOIT tester le résultat.
 */
void *kmalloc(usize size);

/* Comme kmalloc, mais la mémoire est mise à zéro. */
void *kzalloc(usize size);

/*
 * Libère un bloc. kfree(NULL) est permis. PANIC en cas de double libération,
 * de pointeur invalide ou de corruption détectée.
 */
void kfree(void *ptr);

/* Vérifie l'intégrité de tous les blocs (PANIC si corruption). */
void kheap_check(void);

void kheap_get_stats(struct kheap_stats *out);

#endif
