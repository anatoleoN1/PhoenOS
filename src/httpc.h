#ifndef PHOENOS_HTTPC_H
#define PHOENOS_HTTPC_H

#include "types.h"

/*
 * Petit client HTTPS pour le noyau (mises a jour, 0.10.4) : GET, redirections (HTTPS seulement), Content-Length,
 * reponses "chunked". Le transport (connexion TLS) est fourni par l'appelant, ce qui permet de tout tester sans reseau.
 * Le corps n'est pas garde en memoire : il est remis au fur et a mesure a une fonction `sink`.
 */
struct http_net {
    void *ctx;
    int  (*open)(void *ctx, const char *host, u16 port);        /* 0 = ok, sinon un code d'erreur (err_t) */
    int  (*send)(void *ctx, const u8 *data, u32 n);             /* n octets envoyes, ou < 0 */
    int  (*recv)(void *ctx, u8 *buf, u32 cap);                  /* > 0 octets, 0 = fin, < 0 = erreur */
    void (*close)(void *ctx);
};

typedef int (*http_sink)(void *ctx, const u8 *data, u32 n);     /* renvoie 0 pour continuer, autre chose pour arreter */

enum {
    HTTP_OK = 0,
    HTTP_E_OPEN = -1,       /* connexion impossible (code dans result.net_err) */
    HTTP_E_SEND = -2,
    HTTP_E_RECV = -3,
    HTTP_E_HEADER = -4,     /* en-tetes mal formes ou trop gros */
    HTTP_E_STATUS = -5,     /* le serveur repond autre chose que 200 (code dans result.status) */
    HTTP_E_REDIRECT = -6,   /* trop de redirections, adresse invalide ou retour a HTTP non securise */
    HTTP_E_TRUNC = -7,      /* reponse coupee avant la fin annoncee */
    HTTP_E_BIG = -8,        /* corps plus gros que la limite */
    HTTP_E_SINK = -9,       /* l'appelant a arrete le transfert */
    HTTP_E_CHUNK = -10      /* decoupage "chunked" invalide */
};

struct http_result {
    int status;             /* dernier code HTTP recu (0 si aucun) */
    int net_err;            /* code renvoye par open() en cas de HTTP_E_OPEN */
    u32 body;               /* octets de corps remis a sink */
    int redirects;
};

#define HTTP_MAX_REDIRECTS 5
#define HTTP_HEADER_MAX    8192
#define HTTP_PATH_MAX      1536
#define HTTP_HOST_MAX      128

/* host : nom de domaine ; path : commence par '/'. Le TLS (port 443 par defaut) est l'affaire de n->open. */
int http_get(const struct http_net *n, const char *host, const char *path, http_sink sink, void *sctx,
             u32 max_body, struct http_result *r);

const char *http_strerror(int code);

#endif
