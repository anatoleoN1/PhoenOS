#ifndef PHOENOS_POE_H
#define PHOENOS_POE_H

#include "types.h"

/*
 * Format .poe : l'application PhoenOS (version 1).
 * Un .poe = un en-tete de 96 octets (little-endian) suivi d'un programme ELF 32 bits statique.
 * L'en-tete dit ce qu'est l'application (nom, version, auteur) et ce qu'elle exige du systeme,
 * si bien que PhoenOS peut REFUSER proprement une application qu'il ne sait pas encore lancer
 * (interface graphique, reseau, systeme trop ancien) au lieu de la laisser planter.
 *
 *   0  "POE\1"            signature
 *   4  u16 format         1
 *   6  u16 flags          POE_NEEDS_GUI, POE_NEEDS_NET (les autres bits : interdits)
 *   8  u32 min_api        niveau d'API PhoenOS minimal (POE_API_LEVEL du systeme >= min_api)
 *  12  u32 payload_off    debut du programme ELF dans le fichier (>= 96)
 *  16  u32 payload_len    taille du programme ELF
 *  20  u32 payload_crc    CRC-32 du programme (detecte un fichier abime)
 *  24  char name[32]      nom de l'application     (termine par 0)
 *  56  char version[16]   version de l'application (termine par 0)
 *  72  char author[24]    auteur                   (termine par 0)
 *
 * Format 2 (0.8.0) : meme debut, mais l'en-tete fait 208 octets et dit aussi :
 *  96  char description[48]  phrase de presentation (peut etre vide)
 * 144  char needs[4][16]     noms des bibliotheques .psl necessaires (de /lib), les premieres cases seules sont utilisees
 * PhoenOS verifie AVANT de lancer que ces bibliotheques existent et sont valides. Le format 1 reste lu tel quel.
 */
#define POE2_HEADER_SIZE  208u
#define POE_FORMAT_2      2
#define POE_MAX_NEEDS     4
#define POE_LIBNAME_MAX   16            /* taille d'une case (15 caracteres + 0) */
#define POE_HEADER_SIZE   96u
#define POE_FORMAT        1
#define POE_API_LEVEL     2             /* niveau d'API offert par ce PhoenOS (2 : bibliotheques .psl, 0.8.0) */
#define POE_NEEDS_GUI     0x0001u       /* demande l'interface graphique (pas encore disponible) */
#define POE_NEEDS_NET     0x0002u       /* demande le reseau (pas encore disponible) */
#define POE_KNOWN_FLAGS   (POE_NEEDS_GUI | POE_NEEDS_NET)
#define POE_AVAILABLE_FLAGS 0u          /* fonctions reellement disponibles aujourd'hui */

enum poe_status {
    POE_OK = 0,
    POE_NOT_POE,            /* ne commence pas par la signature */
    POE_TRUNCATED,          /* fichier plus court que l'en-tete annonce */
    POE_BAD_HEADER,         /* format, drapeaux, decalages ou textes invalides */
    POE_BAD_CRC,            /* programme abime */
    POE_NEEDS_NEWER,        /* exige un PhoenOS plus recent */
    POE_NEEDS_FEATURE       /* exige une fonction pas encore disponible (GUI, reseau) */
};

struct poe_info {
    char name[33];
    char version[17];
    char author[25];
    int  format;                        /* 1 ou 2 */
    char description[49];               /* format 2 (sinon vide) */
    char needs[POE_MAX_NEEDS][POE_LIBNAME_MAX];
    int  nneeds;
    u32  flags;
    u32  min_api;
    const u8 *payload;
    u32  payload_len;
};

/* Nom de bibliotheque valide : 1 a 15 caracteres parmi a-z 0-9 _ - . */
int         poe_valid_libname(const char *s);

/* Ce qu'on met dans l'en-tete en creant une application (poe_build). Textes ASCII imprimables. */
struct poe_spec {
    const char *name, *version, *author;
    const char *description;            /* NULL ou "" : aucune */
    u32  flags;                         /* POE_NEEDS_* */
    u32  min_api;                       /* 0 : choisi automatiquement (1 pour le format 1, 2 pour le format 2) */
    const char *needs[POE_MAX_NEEDS];   /* bibliotheques exigees (format 2) */
    int  nneeds;
};

/*
 * Emballe un programme ELF dans une application .poe : ecrit l'en-tete puis le programme dans out (cap octets).
 * Format 2 si description ou bibliotheques, sinon format 1. Renvoie la taille ecrite, ou 0 avec *why = la raison.
 * Utilise par la commande poepack de PhoenOS ET par l'outil mkpoe de la machine de developpement.
 */
u32         poe_build(const struct poe_spec *s, const u8 *elf, u32 elf_len, u8 *out, u32 cap, const char **why);

int         poe_is(const u8 *data, u32 len);                        /* signature presente ? */
enum poe_status poe_parse(const u8 *data, u32 len, struct poe_info *out);
const char *poe_status_text(enum poe_status s);

#endif
