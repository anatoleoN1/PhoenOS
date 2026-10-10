#ifndef PHOENOS_ASPACE_H
#define PHOENOS_ASPACE_H

#include "types.h"
#include "error.h"

/*
 * Espace d'adressage d'un programme (0.5.0).
 *
 * Chaque programme a SON repertoire de pages. Il est la copie de celui du noyau (noyau
 * toujours present, mais reserve au ring 3), sauf la case qui couvre la zone des programmes
 * [ASPACE_BASE, ASPACE_END) : elle pointe vers une table de pages propre au programme, dont les
 * pages (cadres de RAM pris au gestionnaire de memoire) n'existent que si on les a mappees.
 * Deux programmes peuvent donc utiliser la meme adresse virtuelle sans jamais se voir, et un
 * programme ne peut toucher que les pages qui lui ont ete donnees (le reste : faute).
 */
#define ASPACE_BASE   0x400000u
#define ASPACE_END    0x800000u                 /* 4 Mio : exactement une table de pages */
#define ASPACE_PAGES  1024u

struct aspace {
    u32 *dir;                                   /* repertoire de pages (1 page) */
    u32 *pt;                                    /* table de pages de la zone des programmes (1 page) */
    u32  pages;                                 /* pages mappees */
};

/* Repertoire du noyau (modele des repertoires de programmes). Appele par paging_init. */
void aspace_set_kernel_dir(const u32 *dir);

/* Cree un espace vide. E_NOMEM : plus de memoire ; E_INVAL : repertoire du noyau inconnu. */
err_t aspace_create(struct aspace *a);

/*
 * Mappe [lo, hi) (arrondi aux pages) avec des cadres neufs mis a zero. Les pages deja
 * mappees sont gardees. E_INVAL : hors de la zone ; E_NOMEM : tout ce qui a ete mappe par cet
 * appel est rendu, l'espace reste comme avant.
 */
err_t aspace_map(struct aspace *a, u32 lo, u32 hi, int writable);

/* Change le droit d'ecriture des pages deja mappees de [lo, hi). Le caller recharge CR3. */
err_t aspace_protect(struct aspace *a, u32 lo, u32 hi, int writable);

/*
 * Le programme peut-il lire (write = 0) ou ecrire (write = 1) [addr, addr + len) ?
 * Toutes les pages doivent etre mappees (et modifiables pour une ecriture).
 */
int aspace_check(const struct aspace *a, u32 addr, u32 len, int write);

/* Rend tous les cadres, la table et le repertoire. Sans danger sur un espace vide ou deja detruit. */
void aspace_destroy(struct aspace *a);

/* Adresse physique du repertoire (valeur de CR3). */
u32 aspace_cr3(const struct aspace *a);

/* Materiel (paging_enable.c) : charge le repertoire du programme / du noyau dans CR3. */
void aspace_activate(const struct aspace *a);
void aspace_activate_kernel(void);

#endif
