#ifndef PHOENOS_ELF_H
#define PHOENOS_ELF_H

#include "types.h"
#include "error.h"

/*
 * Charge un programme ELF 32 bits (x86, executable statique) dans la zone memoire
 * [lo, hi) : `mem` pointe sur l'adresse `lo`. Chaque segment PT_LOAD est copie a son
 * adresse virtuelle, le reste (.bss) est mis a zero. Ne lit jamais hors de `img`, n'ecrit
 * jamais hors de [lo, hi). Renvoie E_NOEXEC si le fichier n'est pas un programme valide.
 */
/* Un segment a charger (PT_LOAD non vide). */
struct elf_seg { u32 lo, hi; int writable; };
#define ELF_MAX_SEGS 8

/*
 * Valide le fichier et liste ses segments sans rien ecrire (pour reserver la memoire avant de
 * charger). E_NOEXEC si invalide ou s'il y a plus de ELF_MAX_SEGS segments ou s'ils se chevauchent.
 */
err_t elf_segments(const u8 *img, u32 size, u32 lo, u32 hi, struct elf_seg *segs, int *nsegs, u32 *entry);

err_t elf_load(const u8 *img, u32 size, u8 *mem, u32 lo, u32 hi, u32 *entry);

#endif
