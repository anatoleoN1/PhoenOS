#ifndef PHOENOS_UPDMAN_H
#define PHOENOS_UPDMAN_H

#include "types.h"
#include "updstore.h"

/*
 * Manifeste d'une mise a jour (texte, signe en entier par Ed25519) :
 *
 *   PHOENOS-MANIFEST 1
 *   edition perf32
 *   version 0.10.4
 *   build 10004
 *   kernel phoenos-perf32-0.10.4.kernel 258472 <sha256 en 64 chiffres hexadecimaux>
 *   initrd phoenos-perf32-0.10.4.initrd 267248 <sha256>
 *   notes <texte libre, 80 caracteres au plus, facultatif>
 *
 * L'analyse est STRICTE : rien d'autre, dans cet ordre, fins de ligne "\n" seules, caracteres ASCII imprimables.
 */
struct upd_manifest {
    char edition[8];
    char version[16];
    u32  build;
    char kname[64];
    u32  ksize;
    u8   khash[32];
    char iname[64];
    u32  isize;
    u8   ihash[32];
    char notes[84];
};

enum {
    UPD_M_OK = 0,
    UPD_M_SIZE = -1,        /* vide ou trop long */
    UPD_M_FORMAT = -2,      /* ligne manquante, en trop, mal ecrite ou caractere interdit */
    UPD_M_EDITION = -3,
    UPD_M_VERSION = -4,
    UPD_M_BUILD = -5,       /* build different de celui deduit de la version */
    UPD_M_FILE = -6,        /* nom de fichier ou taille invalide */
    UPD_M_HASH = -7
};

int upd_man_parse(const char *text, u32 len, struct upd_manifest *m);
const char *upd_man_strerror(int code);

/* Build deduit d'un numero "X.Y.Z" (x*1000000 + y*1000 + z) ; 0 si le numero est mal forme. */
u32 upd_build_of(const char *version);
/* 1 si `edition` est une edition connue (perf32, perf64, pui64) */
int upd_edition_known(const char *edition);

/* 1 si la signature Ed25519 de `text` est valide pour la cle `pk` */
int upd_man_verify(const u8 pk[32], const char *text, u32 len, const u8 sig[64]);

#endif
