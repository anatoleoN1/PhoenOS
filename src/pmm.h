#ifndef PHOENOS_PMM_H
#define PHOENOS_PMM_H

#include "types.h"

struct multiboot_info;

#define PMM_FRAME_SIZE 4096u

/* Lit la carte mémoire de GRUB et prépare l'allocateur de frames (pages de 4 Kio). */
void pmm_init(const struct multiboot_info *mbi);

/* Renvoie l'adresse physique d'une frame libre, ou 0 s'il n'y en a plus. */
u32  pmm_alloc_frame(void);

/* Renvoie l'adresse de `count` frames consécutives, ou 0. */
u32  pmm_alloc_contiguous(u32 count);

/* Libère une frame (PANIC en cas de double libération ou d'adresse invalide). */
void pmm_free_frame(u32 addr);

u32  pmm_usable_frames(void);   /* frames signalées disponibles par le bootloader */
u32  pmm_free_frames(void);     /* frames actuellement libres */

#endif
