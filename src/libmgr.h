#ifndef PHOENOS_LIBMGR_H
#define PHOENOS_LIBMGR_H

#include "types.h"
#include "error.h"
#include "user.h"
#include "psl.h"

/*
 * Gestionnaire de bibliotheques partagees (0.8.0). Les bibliotheques sont des fichiers /lib/<nom>.psl (psl.h).
 * La premiere fois qu'un programme en a besoin, le gestionnaire la lit, la verifie, en garde une copie et prepare ses
 * pages de code (UN exemplaire). Chaque programme qui l'utilise obtient ensuite ces memes pages en lecture seule et ses
 * propres pages de donnees. Quand le dernier programme qui l'utilise s'arrete, tout est rendu a la memoire.
 */
#define LIBMGR_MAX 8                    /* bibliotheques differentes chargees en meme temps */

/*
 * Donne au programme l'acces a la bibliotheque `name` (idempotent). E_NOLIB : fichier absent ou invalide ;
 * E_BUSY : sa plage chevauche une bibliotheque deja utilisee par ce programme ; E_NOMEM ; E_2BIG : PSL_PER_PROC atteint ;
 * E_NOSYS : programme sans espace d'adressage (tests).
 */
err_t libmgr_attach(struct uproc *p, const char *name);

/* attach + adresse du symbole `sym` (NULL : adresse de base). E_NOENT : la bibliotheque n'exporte pas ce symbole. */
err_t libmgr_resolve(struct uproc *p, const char *name, const char *sym, u32 *addr);

/* Rend les bibliotheques du programme (appele a sa fin, avant aspace_destroy). */
void  libmgr_detach_all(struct uproc *p);

/* Etat du gestionnaire : i = 0.. ; renvoie 0 quand il n'y a plus d'entree. */
int   libmgr_info(int i, const char **name, u32 *refs, u32 *lo, u32 *hi, u32 *shared_pages);

#endif
