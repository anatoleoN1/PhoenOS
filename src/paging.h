#ifndef PHOENOS_PAGING_H
#define PHOENOS_PAGING_H

#include "types.h"
#include "error.h"

/*
 * Pagination x86 (2 niveaux, pages de 4 Kio). Tant que les programmes partagent l'espace
 * d'adressage du noyau, la protection repose sur les bits des pages :
 *   - toute la RAM est "identite" (adresse virtuelle = physique), reservee au noyau (U/S = 0) ;
 *   - la page 0 est absente : un pointeur nul dans le noyau provoque une faute au lieu de passer ;
 *   - le code et les constantes du noyau sont en lecture seule ;
 *   - seule la zone des programmes (USER_BASE..USER_END) est accessible depuis le ring 3.
 */
#define PAGE_SIZE   4096u
#define PAGE_P      0x001u              /* presente */
#define PAGE_RW     0x002u              /* ecriture permise */
#define PAGE_US     0x004u              /* accessible au ring 3 */
#define PAGE_PWT    0x008u
#define PAGE_PCD    0x010u              /* cache desactive (memoire video) */

#define PAGING_MAX_TOP 0x40000000u      /* le noyau gere au plus 1 Gio de RAM */

struct paging_cfg {
    u32 map_top;                        /* [0, map_top) est mappe ; multiple de 4096 */
    u32 ro_lo, ro_hi;                   /* lecture seule (0, 0 : aucune) */
    u32 user_lo, user_hi;               /* accessible au ring 3 (0, 0 : aucune) */
    u32 unc_lo, unc_hi;                 /* cache desactive (0, 0 : aucune) */
};

/* Nombre de tables de pages (4 Mio chacune) pour couvrir map_top octets. */
u32 paging_tables_for(u32 map_top);

/*
 * Remplit un repertoire (1024 entrees) et `ntables` tables de 1024 entrees (alignees sur 4 Kio).
 * Les entrees du repertoire contiennent l'adresse de chaque table telle que le noyau la voit
 * (identite). E_INVAL si la configuration est incoherente (rien n'est alors garanti sur dir).
 */
err_t paging_build(u32 *dir, u32 *tables, u32 ntables, const struct paging_cfg *cfg);

/* Parcourt les tables : E_FAULT si non presente ; *flags = droits effectifs (PAGE_P|RW|US). */
err_t paging_lookup(const u32 *dir, u32 vaddr, u32 *paddr, u32 *flags);

/* Noyau : construit les tables et active la pagination. Voir paging_enable.c. */
err_t paging_init(void);
int   paging_enabled(void);
u32   paging_table_bytes(void);         /* memoire occupee par les tables */
u32   paging_mapped_top(void);

#endif
