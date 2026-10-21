#ifndef PHOENOS_UPDATE_H
#define PHOENOS_UPDATE_H

#include "types.h"
#include "error.h"

struct multiboot_info;

/*
 * Mises a jour automatiques signees (0.10.4). Vue d'ensemble : voir updboot.h (demarrage), updstore.h (disque),
 * updman.h (manifeste), httpc.h (telechargement) et docs/GUIDE.md.
 */

/* Etape 0 : tout au debut de kernel_main (apres la verification Multiboot, avant la GDT). Ne revient pas si une
 * version installee est lancee ; revient sans rien faire sinon. */
void update_stage0(const struct multiboot_info *mbi);

/* Tache noyau "maj" : confirme la version lancee apres UPDATE_CONFIRM_S, puis verifie les mises a jour de temps en temps. */
void update_start(void);

/* Commande `maj` du shell (arguments comme argv, argv[0] = "maj"). */
err_t update_command(int argc, char **argv);

/* Message a montrer une fois a l'utilisateur ("mise a jour disponible..."), ou NULL. */
const char *update_take_notice(void);

/* 1 si ce noyau a ete lance par l'etape 0 (version installee) */
int update_relaunched(void);

/* Un pas de la tache de fond (confirmation, verification automatique) : expose pour les tests. */
struct upd_bg { int confirmed; u32 next_check; };
void update_bg_step(struct upd_bg *st, u32 up_s);

void update_reset(void);        /* tests */

#endif
