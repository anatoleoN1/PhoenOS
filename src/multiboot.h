#ifndef PHOENOS_MULTIBOOT_H
#define PHOENOS_MULTIBOOT_H

#include "types.h"

#define MB_FLAG_MEM   (1u << 0)     /* mem_lower / mem_upper valides */
#define MB_FLAG_MMAP  (1u << 6)     /* mmap_length / mmap_addr valides */

/* Début de la structure d'information fournie par GRUB (Multiboot 1). */
struct multiboot_info {
    u32 flags;
    u32 mem_lower;          /* Kio sous 1 Mio       */
    u32 mem_upper;          /* Kio au-dessus de 1 Mio */
    u32 boot_device;
    u32 cmdline;
    u32 mods_count;
    u32 mods_addr;
    u32 syms[4];
    u32 mmap_length;        /* taille de la carte mémoire */
    u32 mmap_addr;          /* adresse de la carte mémoire */
} __attribute__((packed));

/* Entrée de la carte mémoire (size ne compte pas son propre champ). */
struct multiboot_mmap_entry {
    u32 size;
    u32 addr_lo, addr_hi;   /* adresse de début (64 bits en deux moitiés) */
    u32 len_lo, len_hi;     /* longueur (64 bits en deux moitiés)         */
    u32 type;               /* 1 = mémoire disponible                     */
} __attribute__((packed));

#define MB_MMAP_AVAILABLE 1

#endif
