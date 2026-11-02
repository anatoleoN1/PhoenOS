#ifndef PHOENOS_X509_H
#define PHOENOS_X509_H

#include "types.h"

/*
 * Certificats X.509 (0.10.2) : lecture DER, verification de chaine jusqu'a une autorite racine connue, nom d'hote.
 * Sont verifies : signatures (RSA PKCS#1 v1.5 SHA-256/384/512, ECDSA P-256/P-384, Ed25519), dates, autorite de
 * certification (basicConstraints, longueur de chemin, keyUsage), usage serveur (extendedKeyUsage), nom (SAN).
 * Refuses : SHA-1/MD5, RSA < 2048 bits, extension critique inconnue (dont nameConstraints), certificat sans SAN.
 * Pas de verification de revocation (CRL/OCSP) : limite connue, a traiter plus tard.
 */
enum {
    X509_OK = 0,
    X509_E_PARSE = -1,        /* certificat mal forme */
    X509_E_SIGALG = -2,       /* algorithme de signature ou de cle non pris en charge (ou trop faible) */
    X509_E_SIG = -3,          /* signature fausse */
    X509_E_EXPIRED = -4,
    X509_E_NOTYET = -5,
    X509_E_HOST = -6,         /* le nom d'hote ne correspond pas */
    X509_E_UNTRUSTED = -7,    /* aucune autorite racine connue au bout de la chaine */
    X509_E_CA = -8,           /* un maillon n'a pas le droit d'etre autorite */
    X509_E_EXT = -9,          /* extension critique inconnue */
    X509_E_CLOCK = -10,       /* l'horloge du systeme n'est pas fiable */
    X509_E_KEYUSE = -11       /* certificat non destine a un serveur TLS */
};

enum { KEY_NONE = 0, KEY_RSA, KEY_EC_P256, KEY_EC_P384, KEY_ED25519 };
enum { SIG_NONE = 0, SIG_RSA_SHA256, SIG_RSA_SHA384, SIG_RSA_SHA512, SIG_ECDSA_SHA256, SIG_ECDSA_SHA384, SIG_ECDSA_SHA512, SIG_ED25519 };

struct x509 {
    const u8 *der;  u32 derlen;
    const u8 *tbs;  u32 tbslen;                 /* partie signee (avec son en-tete) */
    const u8 *issuer;  u32 issuerlen;           /* Name complet (TLV) */
    const u8 *subject; u32 subjectlen;
    u32 not_before, not_after;                  /* secondes depuis 1970 (plafonnees a 2106) */
    int sig_alg;
    const u8 *sig; u32 siglen;                  /* contenu du BIT STRING, sans l'octet "bits inutilises" */
    int key_type;
    const u8 *rsa_n; u32 rsa_nlen;
    const u8 *rsa_e; u32 rsa_elen;
    const u8 *pub;  u32 publen;                 /* ECDSA : 04||X||Y ; Ed25519 : 32 octets */
    int has_bc, is_ca, pathlen;                 /* pathlen = -1 : sans limite */
    int has_ku;  u32 ku;                        /* bit 0 (0x01) : digitalSignature ; bit 5 (0x20) : keyCertSign */
    int has_eku, eku_server;                    /* serverAuth ou anyExtendedKeyUsage present */
    const u8 *san; u32 sanlen;                  /* contenu de la SEQUENCE GeneralNames, NULL si absent */
};

#define KU_DIGITAL_SIGNATURE 0x01u
#define KU_KEY_CERT_SIGN     0x20u

int  x509_parse(struct x509 *c, const u8 *der, u32 len);
/* 0 si le nom correspond a un dNSName (jocker "*." accepte sur une seule etiquette) ou une adresse IPv4 du SAN. */
int  x509_check_host(const struct x509 *c, const char *host);
/* Signature de `c` par la cle de `issuer`. */
int  x509_check_sig(const struct x509 *c, const struct x509 *issuer);
/* Verifie la chaine envoyee par le serveur (der[0] = certificat du serveur) pour `host` a l'instant `now`. */
int  x509_verify_chain(const u8 *const *der, const u32 *len, int n, const char *host, u32 now);
const char *x509_strerror(int e);

/* Date : secondes depuis 1970 ; 0 si la date est invalide. */
u32  x509_time_from_civil(u32 year, u32 mon, u32 day, u32 hour, u32 min, u32 sec);

/* Autorites racines : par defaut celles de roots.c ; les tests peuvent en fournir d'autres (NULL, 0 = retour au defaut). */
void x509_set_roots(const u8 *const *der, const u32 *len, int n);

#endif
