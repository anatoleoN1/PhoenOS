#ifndef PHOENOS_PERSIST_H
#define PHOENOS_PERSIST_H

#include "types.h"
#include "error.h"
#include "ata.h"
#include "initrd.h"

/*
 * Stockage persistant : sauvegarde du système de fichiers (fs.c) sur un disque ATA.
 *
 * Format « PhoenFS » : le disque est coupé en deux emplacements (A et B) de 8 Mio au plus.
 * Chaque emplacement contient 1 secteur d'en-tête (signature, numéro de génération, taille,
 * CRC des données, CRC de l'en-tête) suivi d'une archive tar. Une sauvegarde écrit toujours dans
 * l'emplacement qui N'EST PAS le courant, puis écrit son en-tête en dernier : si l'alimentation
 * saute en cours d'écriture, l'ancienne sauvegarde reste intacte et sera relue au démarrage.
 */
#define PERSIST_MAX_SLOT_SECTORS 16384          /* 8 Mio par emplacement */
#define PERSIST_MIN_SLOT_SECTORS 64             /* disque minimal : 64 Kio au total */

enum persist_state {
    PERSIST_NO_DISK = 0,        /* aucun disque ATA                              */
    PERSIST_UNFORMATTED,        /* disque présent mais sans PhoenFS : `format`   */
    PERSIST_READY               /* PhoenFS valide : save / load possibles         */
};

struct persist_info {
    int  state;
    char model[41];
    u32  sectors;               /* capacité du disque                         */
    u32  slot_sectors;          /* taille d'un emplacement                    */
    int  active;                /* emplacement courant (0 = A, 1 = B, -1 = aucun) */
    u32  generation;            /* numéro de la dernière sauvegarde            */
    u32  snapshot_bytes;        /* taille de la dernière sauvegarde (tar)      */
};

/* Cherche le disque et lit les en-têtes. Ne modifie jamais le disque. */
err_t persist_init(void);

int persist_state(void);
const struct persist_info *persist_get_info(void);

/*
 * Prépare le disque : écrit une sauvegarde vide dans les deux emplacements (génération 1 et 0).
 * Refuse (E_BUSY) si le premier secteur du disque contient déjà des données qui ne sont pas du PhoenFS,
 * sauf si `force` est vrai.
 */
err_t persist_format(int force);

/* Écrit une nouvelle sauvegarde complète du système de fichiers. */
err_t persist_save(void);

/* Relit la dernière sauvegarde valide PAR-DESSUS le système de fichiers actuel (les fichiers de même nom sont remplacés). */
err_t persist_load(struct initrd_stats *st);

/* Vrai si le système de fichiers a changé depuis la dernière sauvegarde (ou le dernier chargement). */
int persist_dirty(void);

/* Sauvegarde seulement si un disque prêt existe ET que des modifications existent. E_OK sinon rien à faire. */
err_t persist_autosave(void);

/* Déclare le contenu actuel comme déjà enregistré (appelé après le chargement au démarrage). */
void persist_mark_clean(void);

#endif
