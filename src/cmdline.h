#ifndef PHOENOS_CMDLINE_H
#define PHOENOS_CMDLINE_H

#include "types.h"
#include "error.h"

/*
 * Découpe `line` (modifiée sur place) en arguments séparés par des espaces.
 * Les guillemets doubles regroupent plusieurs mots : echo "a   b"  -> 1 argument.
 * Renvoie le nombre d'arguments (argv[argc] = NULL), E_2BIG si trop d'arguments
 * (> max), ou E_INVAL si un guillemet n'est pas fermé.
 */
int cmdline_split(char *line, char **argv, int max);

/* Convertit "123" ou "0x1F" en nombre. E_INVAL si vide, mal formé ou trop grand. */
err_t cmdline_parse_u32(const char *s, u32 *out);

/* ---- Historique des commandes (pur logiciel, sans affichage) ---- */

#define HIST_MAX 16
#define HIST_LEN 128

struct history {
    char entry[HIST_MAX][HIST_LEN];
    int  count;                 /* entrées mémorisées (<= HIST_MAX)          */
    int  start;                 /* indice de la plus ancienne                */
    int  pos;                   /* position de navigation : count = nouvelle ligne */
};

void history_init(struct history *h);

/* Mémorise une ligne (ignore les lignes vides et la répétition de la dernière). */
void history_add(struct history *h, const char *line);

/* i = 0 : la plus ancienne. NULL si hors limites. */
const char *history_get(const struct history *h, int i);

/* Remonte d'une entrée. NULL si déjà sur la plus ancienne (ou historique vide). */
const char *history_prev(struct history *h);

/* Redescend. Renvoie "" en arrivant sur la ligne en cours ; NULL si on y était déjà. */
const char *history_next(struct history *h);

/* Vrai si on n'est pas en train de naviguer (ligne en cours d'édition). */
int history_at_end(const struct history *h);

#endif
