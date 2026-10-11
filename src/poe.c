/* PhoenOS - poe.c : lecture et verification des applications .poe (voir poe.h). */
#include "poe.h"
#include "crc32.h"
#include "string.h"

static u32 rd32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static u32 rd16(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8; }

int poe_is(const u8 *data, u32 len)
{
    return len >= 4 && data[0] == 'P' && data[1] == 'O' && data[2] == 'E' && data[3] == 1;
}

/* Copie un texte de l'en-tete : doit etre termine par 0 dans sa zone, en ASCII imprimable. */
static int copy_text(char *dst, const u8 *src, u32 size)
{
    u32 i;

    for (i = 0; i < size && src[i]; i++) {
        if (src[i] < 32 || src[i] > 126)
            return 0;
        dst[i] = (char)src[i];
    }
    if (i == size || i == 0)
        return 0;                                   /* pas de 0 final, ou texte vide */
    for (u32 k = i; k < size; k++)
        if (src[k])
            return 0;                               /* pas de bribes apres le 0 */
    dst[i] = 0;
    return 1;
}

int poe_valid_libname(const char *s)
{
    usize n = strlen(s);

    if (n == 0 || n >= POE_LIBNAME_MAX)
        return 0;
    for (usize i = 0; i < n; i++) {
        char c = s[i];

        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return 0;
    }
    return 1;
}

/* Texte facultatif (peut etre vide) : ASCII imprimable, 0 final, rien apres. */
static int copy_text_opt(char *dst, const u8 *src, u32 size)
{
    u32 i;

    for (i = 0; i < size && src[i]; i++) {
        if (src[i] < 32 || src[i] > 126)
            return 0;
        dst[i] = (char)src[i];
    }
    if (i == size)
        return 0;
    for (u32 k = i; k < size; k++)
        if (src[k])
            return 0;
    dst[i] = 0;
    return 1;
}

/* Les 4 cases de bibliotheques du format 2 : noms valides, sans trou, sans doublon. */
static int parse_needs(struct poe_info *out, const u8 *src)
{
    int n = 0, ended = 0;

    for (int i = 0; i < POE_MAX_NEEDS; i++) {
        char tmp[POE_LIBNAME_MAX];

        if (!copy_text_opt(tmp, src + i * POE_LIBNAME_MAX, POE_LIBNAME_MAX))
            return 0;
        if (!tmp[0]) {
            ended = 1;
            continue;
        }
        if (ended || !poe_valid_libname(tmp))
            return 0;
        for (int k = 0; k < n; k++)
            if (!strcmp(out->needs[k], tmp))
                return 0;
        strcpy(out->needs[n++], tmp);
    }
    out->nneeds = n;
    return 1;
}

enum poe_status poe_parse(const u8 *d, u32 len, struct poe_info *out)
{
    u32 flags, off, plen, hdr;

    memset(out, 0, sizeof(*out));
    if (!poe_is(d, len))
        return POE_NOT_POE;
    if (len < POE_HEADER_SIZE)
        return POE_TRUNCATED;
    out->format = (int)rd16(d + 4);
    if (out->format == POE_FORMAT_2) {
        hdr = POE2_HEADER_SIZE;
        if (len < hdr)
            return POE_TRUNCATED;
    } else if (out->format == POE_FORMAT) {
        hdr = POE_HEADER_SIZE;
    } else {
        return POE_BAD_HEADER;
    }
    flags = rd16(d + 6);
    if (flags & ~POE_KNOWN_FLAGS)
        return POE_BAD_HEADER;
    off = rd32(d + 12);
    plen = rd32(d + 16);
    if (off < hdr || plen == 0)
        return POE_BAD_HEADER;
    if (off > len || plen > len - off)
        return POE_TRUNCATED;
    if (!copy_text(out->name, d + 24, 32) || !copy_text(out->version, d + 56, 16) ||
        !copy_text(out->author, d + 72, 24))
        return POE_BAD_HEADER;
    if (out->format == POE_FORMAT_2 &&
        (!copy_text_opt(out->description, d + 96, 48) || !parse_needs(out, d + 144)))
        return POE_BAD_HEADER;
    out->flags = flags;
    out->min_api = rd32(d + 8);
    out->payload = d + off;
    out->payload_len = plen;
    if (crc32_update(0, out->payload, plen) != rd32(d + 20))
        return POE_BAD_CRC;
    if (out->min_api > POE_API_LEVEL)
        return POE_NEEDS_NEWER;
    if (flags & ~POE_AVAILABLE_FLAGS)
        return POE_NEEDS_FEATURE;
    return POE_OK;
}

const char *poe_status_text(enum poe_status s)
{
    switch (s) {
    case POE_OK:           return "valide";
    case POE_NOT_POE:      return "ce n'est pas une application .poe";
    case POE_TRUNCATED:    return "fichier .poe tronque";
    case POE_BAD_HEADER:   return "en-tete .poe invalide";
    case POE_BAD_CRC:      return "programme abime (somme de controle incorrecte)";
    case POE_NEEDS_NEWER:  return "exige une version plus recente de PhoenOS";
    case POE_NEEDS_FEATURE: return "exige une fonction pas encore disponible (interface graphique ou reseau)";
    }
    return "erreur inconnue";
}

/* ---- creation ---- */

static void wr16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void wr32(u8 *p, u32 v) { wr16(p, v & 0xFFFF); wr16(p + 2, v >> 16); }

/* Ecrit un texte dans sa case (deja a zero). 0 si vide (sauf allow_empty), trop long ou non imprimable. */
static int put_text(u8 *dst, u32 size, const char *s, int allow_empty)
{
    usize n = s ? strlen(s) : 0;

    if (n == 0)
        return allow_empty;
    if (n >= size)
        return 0;
    for (usize i = 0; i < n; i++)
        if ((u8)s[i] < 32 || (u8)s[i] > 126)
            return 0;
    memcpy(dst, s, n);
    return 1;
}

u32 poe_build(const struct poe_spec *s, const u8 *elf, u32 elf_len, u8 *out, u32 cap, const char **why)
{
    int v2 = (s->description && s->description[0]) || s->nneeds > 0;
    u32 hdr = v2 ? POE2_HEADER_SIZE : POE_HEADER_SIZE;
    const char *dummy;

    if (!why)
        why = &dummy;
    if (s->flags & ~POE_KNOWN_FLAGS) { *why = "drapeaux inconnus"; return 0; }
    if (s->nneeds < 0 || s->nneeds > POE_MAX_NEEDS) { *why = "trop de bibliotheques (4 au plus)"; return 0; }
    if (!elf || elf_len < 52 || elf[0] != 0x7F || elf[1] != 'E' || elf[2] != 'L' || elf[3] != 'F') {
        *why = "le programme n'est pas un fichier ELF";
        return 0;
    }
    if (elf_len > (4u << 20) || hdr + elf_len > cap) { *why = "programme trop gros"; return 0; }
    memset(out, 0, hdr);
    memcpy(out, "POE\1", 4);
    wr16(out + 4, v2 ? POE_FORMAT_2 : POE_FORMAT);
    wr16(out + 6, s->flags);
    wr32(out + 8, s->min_api ? s->min_api : (v2 ? 2u : 1u));
    wr32(out + 12, hdr);
    wr32(out + 16, elf_len);
    wr32(out + 20, crc32_update(0, elf, elf_len));
    if (!put_text(out + 24, 32, s->name, 0)) { *why = "nom : 1 a 31 caracteres ASCII imprimables"; return 0; }
    if (!put_text(out + 56, 16, s->version, 0)) { *why = "version : 1 a 15 caracteres ASCII imprimables"; return 0; }
    if (!put_text(out + 72, 24, s->author, 0)) { *why = "auteur : 1 a 23 caracteres ASCII imprimables"; return 0; }
    if (v2) {
        if (!put_text(out + 96, 48, s->description, 1)) { *why = "description : 47 caracteres ASCII imprimables au plus"; return 0; }
        for (int i = 0; i < s->nneeds; i++) {
            if (!s->needs[i] || !poe_valid_libname(s->needs[i])) { *why = "nom de bibliotheque invalide (a-z 0-9 _ -, 15 au plus)"; return 0; }
            for (int k = 0; k < i; k++)
                if (!strcmp(s->needs[k], s->needs[i])) { *why = "bibliotheque indiquee deux fois"; return 0; }
            put_text(out + 144 + i * POE_LIBNAME_MAX, POE_LIBNAME_MAX, s->needs[i], 0);
        }
    }
    memcpy(out + hdr, elf, elf_len);
    return hdr + elf_len;
}
