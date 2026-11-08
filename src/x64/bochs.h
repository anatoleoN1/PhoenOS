#ifndef PHOENOS_X64_BOCHS_H
#define PHOENOS_X64_BOCHS_H

#include "types.h"
#include "error.h"

/*
 * Changement de resolution en direct (0.12.0) pour la carte graphique "VGA standard" de QEMU (interface Bochs VBE
 * "dispi", ports 0x1CE / 0x1CF, PCI 1234:1111). Sur une autre carte (materiel reel), on ne peut pas changer de mode
 * apres le demarrage : la resolution se choisit alors dans le menu de demarrage (GRUB).
 */
struct bochs_mode { u16 w, h; };

int   bochs_present(void);                              /* 1 : la carte sait changer de mode */
u32   bochs_vram(void);                                 /* memoire video en octets (0 si inconnue) */
int   bochs_modes(struct bochs_mode *out, int max);     /* modes proposes (ceux qui tiennent dans la memoire video) */
err_t bochs_set_mode(u32 w, u32 h);                     /* E_NOSYS : pas de carte compatible ; E_INVAL : mode refuse */

/* Teste si (w, h) tient dans `vram` octets en 32 bits par pixel avec des bornes raisonnables. */
int   bochs_mode_ok(u32 w, u32 h, u32 vram);

#endif
