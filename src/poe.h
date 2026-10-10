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
 */
#define POE_HEADER_SIZE   96u
#define POE_FORMAT        1
#define POE_API_LEVEL     1             /* niveau d'API offert par ce PhoenOS */
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
    u32  flags;
    u32  min_api;
    const u8 *payload;
    u32  payload_len;
};

int         poe_is(const u8 *data, u32 len);                        /* signature presente ? */
enum poe_status poe_parse(const u8 *data, u32 len, struct poe_info *out);
const char *poe_status_text(enum poe_status s);

#endif
