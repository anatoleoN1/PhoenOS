#ifndef PHOENOS_TLS_H
#define PHOENOS_TLS_H

#include "types.h"
#include "crypto.h"

/*
 * Client TLS 1.3 (0.10.2), RFC 8446. Independant du reseau : l'appelant fournit deux fonctions (envoyer, recevoir).
 *  - Suites : TLS_AES_128_GCM_SHA256, TLS_AES_256_GCM_SHA384, TLS_CHACHA20_POLY1305_SHA256.
 *  - Echange de cles : X25519. Le serveur est authentifie par sa chaine de certificats (x509.h) et CertificateVerify
 *    (ECDSA P-256/P-384, RSA-PSS, Ed25519), puis par son message Finished.
 *  - Pas de TLS 1.2 ni plus ancien (refuse : un serveur sans TLS 1.3 donne TLS_E_UNSUPPORTED), pas de reprise de session,
 *    pas de certificat client (une demande recoit un certificat vide), pas de donnees 0-RTT, pas de HelloRetryRequest.
 *  - KeyUpdate recu : pris en charge. Les tickets de session recus sont ignores.
 */
enum {
    TLS_OK = 0,
    TLS_E_IO = -1,            /* le transport a echoue ou le serveur a coupe */
    TLS_E_PROTOCOL = -2,      /* message inattendu ou mal forme */
    TLS_E_ALERT = -3,         /* le serveur a envoye une alerte (voir t->alert) */
    TLS_E_CERT = -4,          /* certificat refuse (voir t->cert_err, codes de x509.h) */
    TLS_E_VERIFY = -5,        /* CertificateVerify ou Finished faux */
    TLS_E_UNSUPPORTED = -6,   /* le serveur ne propose rien que nous sachions faire */
    TLS_E_RNG = -7,           /* generateur aleatoire pas assez alimente */
    TLS_E_DECRYPT = -8,       /* enregistrement dont l'authentification echoue */
    TLS_E_CLOSED = -9,        /* connexion deja fermee */
    TLS_E_TOOBIG = -10        /* message du serveur trop grand pour nos tampons */
};

struct tls_io {
    /* Envoie tout ou echoue. 0 = ok, < 0 = erreur. */
    int (*send)(void *ctx, const u8 *data, u32 len);
    /* Lit au plus cap octets : > 0 octets lus, 0 = le serveur a ferme, < 0 = erreur ou delai. */
    int (*recv)(void *ctx, u8 *buf, u32 cap);
    void *ctx;
};

#define TLS_REC_MAX     16384u
#define TLS_HS_MAX      24000u

struct tls_dir {
    u8  secret[HASH_MAX];
    u8  key[32];
    u8  iv[12];
    u32 seq_hi, seq_lo;
    int encrypted;
    aesgcm_ctx gcm;
};

struct tls {
    struct tls_io io;
    int suite, hash, keylen, hashlen;     /* suite : 0x1301 / 0x1302 / 0x1303 */
    int established, closed, failed;
    int alert;                            /* derniere alerte recue (code TLS) */
    int cert_err;                         /* code x509 en cas de TLS_E_CERT */
    struct tls_dir rx, tx;
    hash_ctx th;                          /* condense de toutes les poignees de main */
    u8  master[HASH_MAX];
    /* tampons */
    u8  rec[5 + TLS_REC_MAX + 256];       /* enregistrement recu (dechiffre sur place) */
    u8  out[5 + TLS_REC_MAX + 1 + 16];    /* enregistrement a envoyer */
    u8  hs[TLS_HS_MAX];                   /* messages de poignee de main, mis bout a bout */
    u32 hs_len;
    const u8 *app; u32 app_len;           /* donnees applicatives pas encore remises */
};

/* Poignee de main complete. host : nom pour SNI et verification du certificat. now : secondes depuis 1970 (RTC).
 * Renvoie TLS_OK ou un code TLS_E_*. En cas d'echec la structure ne doit plus servir. */
int  tls_connect(struct tls *t, const struct tls_io *io, const char *host, u32 now);
/* Envoie len octets (decoupes en enregistrements). Renvoie len ou une erreur. */
int  tls_send(struct tls *t, const u8 *data, u32 len);
/* Recoit des donnees : > 0 octets, 0 = le serveur a ferme proprement (close_notify), < 0 = erreur. */
int  tls_recv(struct tls *t, u8 *buf, u32 cap);
/* Envoie close_notify (sans attendre la reponse). */
void tls_close(struct tls *t);
const char *tls_strerror(int e);

#endif
