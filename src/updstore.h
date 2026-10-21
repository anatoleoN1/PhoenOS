#ifndef PHOENOS_UPDSTORE_H
#define PHOENOS_UPDSTORE_H

#include "types.h"
#include "error.h"

/*
 * Zone de mises a jour sur le disque (0.10.4). Elle commence APRES les deux emplacements PhoenFS
 * (2 x 8 Mio) : secteurs 32768 a 65535 (16 Mio, disque de 32 Mio minimum).
 *
 *   secteur 32768, 32769 : deux copies de l'etat (generation la plus haute et valide = la bonne)
 *   secteur 32832        : emplacement 0 ; puis 16352 secteurs plus loin : emplacement 1
 *   dans un emplacement  : +0 en-tete (ecrit EN DERNIER) | +1,+2 signature+manifeste | +3 noyau | +4099 initrd
 *
 * Toute ecriture est faite pour qu'une coupure de courant laisse l'ancien etat intact :
 * l'en-tete d'un emplacement est annule avant d'y ecrire, et l'etat n'est modifie qu'apres.
 */
#define UPD_SECTOR        512u
#define UPD_BASE_LBA      32768u
#define UPD_AREA_SECTORS  32768u
#define UPD_SLOT0_LBA     (UPD_BASE_LBA + 64u)
#define UPD_SLOT_SECTORS  16352u
#define UPD_MAN_OFF       1u
#define UPD_KERNEL_OFF    3u
#define UPD_KERNEL_MAX    4096u                 /* secteurs : 2 Mio */
#define UPD_INITRD_OFF    4099u
#define UPD_INITRD_MAX    12253u                /* secteurs : 6273536 octets */
#define UPD_KERNEL_MAX_BYTES (UPD_KERNEL_MAX * UPD_SECTOR)
#define UPD_INITRD_MAX_BYTES (UPD_INITRD_MAX * UPD_SECTOR)
#define UPD_MAN_MAX       900u

/* Un disque pour la zone (le noyau : ATA ; les tests : un tableau en memoire). */
struct upd_blk {
    u32 sectors;
    void *ctx;
    err_t (*rd)(void *ctx, u32 lba, u32 count, void *buf);
    err_t (*wr)(void *ctx, u32 lba, u32 count, const void *buf);
    err_t (*flush)(void *ctx);
};

struct upd_ctl {
    u32 gen;
    i32 active;             /* -1 : le noyau du CD/de l'ISO ; 0 ou 1 : emplacement */
    u32 tries;              /* demarrages tentes depuis la derniere confirmation */
    u32 trial;              /* 1 : l'emplacement actif n'a pas encore ete confirme */
    u32 min_build;          /* plus haut build confirme : on n'accepte jamais plus ancien (anti-retour-arriere) */
    u32 autom;              /* 0 rien, 1 verifier seulement, 2 verifier et installer */
};

struct upd_slot {
    u32 build;
    u32 kernel_bytes, initrd_bytes;
    u32 man_len;
    u8  sig[64];
    char man[UPD_MAN_MAX + 1];
};

/* E_NOSYS : disque trop petit (< 32 Mio) */
err_t upd_area_check(const struct upd_blk *b);
/* E_NOENT : zone jamais preparee ; E_INVAL : etat illisible (secteurs abimes) */
err_t upd_ctl_load(const struct upd_blk *b, struct upd_ctl *c);
/* Ecrit une nouvelle generation dans la copie qui n'est PAS la derniere valide ; met a jour c->gen. */
err_t upd_ctl_store(const struct upd_blk *b, struct upd_ctl *c);
/* Prepare la zone (etat vierge). E_BUSY si les secteurs d'etat contiennent deja autre chose et !force. Idempotent. */
err_t upd_area_prepare(const struct upd_blk *b, int force);

/* E_NOENT : emplacement vide ou en-tete invalide */
err_t upd_slot_load(const struct upd_blk *b, int slot, struct upd_slot *s);
err_t upd_slot_invalidate(const struct upd_blk *b, int slot);
/* Ecrit `count` secteurs de donnees a l'adresse relative `off` de l'emplacement (verifie les bornes). */
err_t upd_slot_write(const struct upd_blk *b, int slot, u32 off, u32 count, const void *buf);
err_t upd_slot_read(const struct upd_blk *b, int slot, u32 off, u32 count, void *buf);
/* Ecrit signature + manifeste, vide le cache, puis l'en-tete en dernier. */
err_t upd_slot_commit(const struct upd_blk *b, int slot, u32 build, u32 kernel_bytes, u32 initrd_bytes,
                      const char *man, u32 man_len, const u8 sig[64]);

#endif
