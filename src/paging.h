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

/*
 * Mappe en identite [lo, hi) (arrondi aux pages) avec les droits `flags` (PAGE_P est ajoute), pour du materiel mappe en memoire
 * (cartes PCI) hors de la RAM. Cree au besoin la table de pages de chaque 4 Mio avec alloc() (une page, a zero ou non :
 * elle est remplie ici). E_INVAL : zone vide ou deja (en partie) presente ; E_NOMEM : alloc() a echoue (rien n'est change
 * pour les tables deja existantes ; une table neuve deja posee reste vide).
 */
err_t paging_map_range(u32 *dir, u32 lo, u32 hi, u32 flags, u32 (*alloc)(void));

/* Noyau : mappe une plage de materiel (voir paging_map_range) dans le repertoire du noyau. A faire AVANT le premier programme
 * (les espaces des programmes copient ce repertoire a leur creation). Seul le noyau (taches du noyau) touche ces adresses. */
err_t paging_map_mmio(u32 phys, u32 size);

/* Noyau : construit les tables et active la pagination. Voir paging_enable.c. */
err_t paging_init(void);
int   paging_enabled(void);
u32   paging_table_bytes(void);         /* memoire occupee par les tables */
u32   paging_mapped_top(void);

#endif
