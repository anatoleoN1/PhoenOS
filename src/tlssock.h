#ifndef PHOENOS_TLSSOCK_H
#define PHOENOS_TLSSOCK_H

#include "types.h"
#include "error.h"

/*
 * Connexions securisees (HTTPS) pour les programmes (0.10.2) : une connexion TCP + une session TLS 1.3 (tls.c).
 * Limite volontaire : UNE seule session TLS a la fois dans tout le systeme (la session occupe ~57 Kio de memoire) ;
 * elle est reservee au programme qui l'a ouverte et fermee quand il se termine.
 */
#define TLSS_MAX 1

void  tlss_reset(void);
int   tlss_open_count(void);

/* host : nom pour le certificat et SNI ; ip : adresse deja resolue. E_BUSY si une session est deja ouverte ;
 * E_CERT si le certificat est refuse ; E_TLS pour toute autre erreur TLS ; E_TIMEOUT / E_REFUSED / ... venant de TCP. */
err_t tlss_connect(u32 owner, const char *host, u32 ip, u16 port, int *h);
/* Envoie len octets (tout est envoye) ; renvoie len ou un code d'erreur. */
i32   tlss_send(u32 owner, int h, const u8 *data, u32 len);
/* > 0 octets recus, 0 = fin normale (close_notify), < 0 erreur. */
i32   tlss_recv(u32 owner, int h, u8 *buf, u32 cap);
err_t tlss_close(u32 owner, int h);
void  tlss_release_owner(u32 owner);

/* Texte (francais) de la derniere erreur TLS ou de certificat, pour que le programme l'affiche. Jamais NULL. */
const char *tlss_last_error(void);

#endif
