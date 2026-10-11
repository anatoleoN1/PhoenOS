#ifndef PHOENOS_PSL_H
#define PHOENOS_PSL_H

#include "types.h"
#include "elf.h"

/*
 * Format .psl : bibliotheque partagee PhoenOS (0.8.0).
 *
 * Une bibliotheque est un programme ELF 32 bits statique LIE A UNE ADRESSE FIXE dans la zone des bibliotheques
 * [PSL_LIB_LO, PSL_LIB_HI) (pas de relocation : chaque bibliotheque a sa plage, deux bibliotheques dont les plages se
 * chevauchent ne peuvent pas etre utilisees ensemble). Son CODE et ses constantes (segments non modifiables) existent en
 * UN SEUL exemplaire en memoire, partage en lecture seule par tous les programmes qui l'utilisent ; ses DONNEES
 * (segments modifiables) sont recopiees pour chaque programme : les variables globales d'une bibliotheque sont donc
 * propres a chaque programme, comme celles du programme lui-meme.
 *
 * Fichier = en-tete de 456 octets (little-endian) + ELF :
 *   0  "PSL\1"            signature
 *   4  u16 format         1
 *   6  u16 flags          0 (reserve)
 *   8  u32 min_api        niveau d'API PhoenOS minimal (POE_API_LEVEL >= min_api)
 *  12  u32 payload_off    debut de l'ELF (>= 456)
 *  16  u32 payload_len    taille de l'ELF
 *  20  u32 payload_crc    CRC-32 de l'ELF
 *  24  char name[16]      nom (a-z 0-9 _ -), identique au nom du fichier : /lib/<nom>.psl
 *  40  char version[16]
 *  56  char author[24]
 *  80  char description[48]   (peut etre vide)
 * 128  u32 nexports       0 a 16
 * 132  u32 0              reserve
 * 136  struct { char name[16]; u32 addr; } exports[16]    fonctions et variables offertes (adresse dans la plage de la bibliotheque)
 */
#define PSL_HEADER_SIZE   456u
#define PSL_FORMAT        1
#define PSL_MAX_EXPORTS   16
#define PSL_SYM_MAX       16            /* taille d'une case de nom de symbole (15 caracteres + 0) */
#define PSL_LIB_LO        0x700000u     /* zone des bibliotheques dans l'espace d'un programme */
#define PSL_LIB_HI        0x7F0000u     /* (juste sous la pile) */
#define PSL_MAX_PAGES     64u           /* 256 Kio de code partage au plus par bibliotheque */
#define PSL_MAX_IMAGE     (512u * 1024u)

enum psl_status {
    PSL_OK = 0,
    PSL_NOT_PSL,            /* ne commence pas par la signature */
    PSL_TRUNCATED,
    PSL_BAD_HEADER,
    PSL_BAD_CRC,
    PSL_NEEDS_NEWER,
    PSL_BAD_IMAGE           /* l'ELF n'est pas une bibliotheque valable (hors zone, segments qui se partagent une page...) */
};

struct psl_export { char name[PSL_SYM_MAX]; u32 addr; };

struct psl_info {
    char name[17];
    char version[17];
    char author[25];
    char description[49];
    u32  min_api;
    int  nexports;
    struct psl_export exports[PSL_MAX_EXPORTS];
    const u8 *payload;
    u32  payload_len;
    u32  lo, hi;                        /* plage occupee (arrondie aux pages) */
    struct elf_seg segs[ELF_MAX_SEGS];
    int  nseg;
};

/* Nom de symbole exporte valide : 1 a 15 caracteres parmi A-Z a-z 0-9 _ */
int         psl_valid_symname(const char *s);

int         psl_is(const u8 *data, u32 len);
enum psl_status psl_parse(const u8 *data, u32 len, struct psl_info *out);
const char *psl_status_text(enum psl_status s);

/* Cherche un symbole exporte : son adresse, ou 0. */
u32         psl_find(const struct psl_info *info, const char *sym);

/* Adresse d'un symbole global de l'ELF (table .symtab : l'ELF ne doit pas etre "strip"). 1 si trouve, sinon 0. */
int         psl_elf_symbol(const u8 *elf, u32 len, const char *name, u32 *addr);

struct psl_spec {
    const char *name, *version, *author, *description;
    u32 min_api;                        /* 0 : 2 */
    int nexports;
    struct { const char *name; u32 addr; } exports[PSL_MAX_EXPORTS];       /* addr 0 : cherchee dans la table des symboles de l'ELF */
};

/* Emballe un ELF en bibliotheque .psl (et verifie le resultat). Renvoie la taille ecrite, ou 0 avec *why. */
u32         psl_build(const struct psl_spec *s, const u8 *elf, u32 elf_len, u8 *out, u32 cap, const char **why);

#endif
