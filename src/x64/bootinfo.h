#ifndef PHOENOS_X64_BOOTINFO_H
#define PHOENOS_X64_BOOTINFO_H

#include "types.h"

/* Ecran fourni par le bootloader (Multiboot2, tag 8). type : 0 aucun, 1 pixels RGB, 2 texte VGA (EGA) */
struct boot_fb {
    u64 addr;
    u32 pitch, width, height;
    u8  bpp, type;
    u8  red_pos, red_size, green_pos, green_size, blue_pos, blue_size;
};

extern struct boot_fb boot_fb;

#define BOOT_NAME_OFFSET 64             /* Multiboot 1 : champ "boot_loader_name" (flags bit 9) */

struct multiboot_info;

/*
 * Convertit les informations Multiboot2 (adresse physique `mb2`) en structure Multiboot 1 (celle que lisent pmm_init,
 * load_initrd, update_stage0) et remplit boot_fb. Appelee par boot.S. Si les infos sont illisibles, renvoie une
 * structure sans carte memoire (le noyau s'arretera avec un message clair).
 */
struct multiboot_info *mb2_convert(usize mb2);

/* Noyau lance par une mise a jour (etape 0) : infos deja en Multiboot 1 ; reprend l'ecran (flag 12). */
struct multiboot_info *mb1_adopt(usize mb1);

/* Version testable : la carte memoire et les modules sont lus dans `mb2`, la sortie va dans `out` (128 octets alignes). */
struct multiboot_info *mb2_convert_into(const void *mb2, u8 *out);

#endif
