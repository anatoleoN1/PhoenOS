#ifndef PHOENOS_DLOG_H
#define PHOENOS_DLOG_H

#include "types.h"

/*
 * Journal de debogage (0.6.2). Tout evenement notable du systeme y est note, avec l'heure :
 * demarrage, commandes tapees, touches, programmes lances / termines / fautifs, appels systeme,
 * ordonnanceur, disque... et, via l'appel systeme log(), les messages des programmes eux-memes.
 *
 * Deux destinations, SANS rien afficher a l'ecran :
 *  - un anneau en memoire (1024 lignes) que l'on lit avec la commande `log` ;
 *  - le "port de debogage" 0xE9 quand il existe (QEMU -debugcon, Bochs) : `make debug` y branche un fichier
 *    (logs/<session>/os.log) qui garde TOUT depuis le demarrage, meme si l'OS plante ou redemarre.
 * Le port est detecte automatiquement : s'il est la, tous les niveaux sont enregistres (TRACE compris) ;
 * sinon on garde DEBUG et plus (modifiable : `log level trace`).
 */
enum dlog_level { DL_TRACE = 0, DL_DEBUG, DL_INFO, DL_WARN, DL_ERROR };

#define DLOG_TAG_MAX  9                  /* 8 caracteres + 0 */
#define DLOG_MSG_MAX  110
#define DLOG_ENTRIES  1024

/* Detecte le port de debogage et fixe le niveau par defaut. A appeler tres tot (avant tout dlog utile). */
void dlog_init(void);

void dlog(int level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

/* Raccourcis */
#define DTRACE(tag, ...) dlog(DL_TRACE, tag, __VA_ARGS__)
#define DDEBUG(tag, ...) dlog(DL_DEBUG, tag, __VA_ARGS__)
#define DINFO(tag, ...)  dlog(DL_INFO,  tag, __VA_ARGS__)
#define DWARN(tag, ...)  dlog(DL_WARN,  tag, __VA_ARGS__)
#define DERROR(tag, ...) dlog(DL_ERROR, tag, __VA_ARGS__)

int         dlog_level(void);                    /* niveau minimal enregistre */
void        dlog_set_level(int level);
int         dlog_parse_level(const char *name);  /* "trace" "debug" "info" "warn" "error" ou 0-4 ; -1 si inconnu */
const char *dlog_level_name(int level);          /* "TRACE" ... (5 lettres, completees par des espaces) */
int         dlog_port_present(void);
void        dlog_panic_mode(void);               /* panique : tout enregistrer, meme si un dlog etait en cours */

u32  dlog_total(void);                           /* lignes enregistrees depuis le demarrage */
u32  dlog_kept(void);                            /* lignes encore dans l'anneau */
void dlog_clear(void);

/* Ligne n (0 = la plus ancienne gardee). Renvoie 0 si n est hors limite. */
int  dlog_get(u32 n, u32 *ms, int *level, char *tag, char *msg);

/* Formate une ligne "[ssss.mmm NIVEAU tag     ] message" dans buf. Renvoie sa longueur. */
usize dlog_format(char *buf, usize size, u32 ms, int level, const char *tag, const char *msg);

#endif
