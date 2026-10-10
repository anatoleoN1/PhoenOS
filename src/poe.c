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

enum poe_status poe_parse(const u8 *d, u32 len, struct poe_info *out)
{
    u32 flags, off, plen;

    memset(out, 0, sizeof(*out));
    if (!poe_is(d, len))
        return POE_NOT_POE;
    if (len < POE_HEADER_SIZE)
        return POE_TRUNCATED;
    if (rd16(d + 4) != POE_FORMAT)
        return POE_BAD_HEADER;
    flags = rd16(d + 6);
    if (flags & ~POE_KNOWN_FLAGS)
        return POE_BAD_HEADER;
    off = rd32(d + 12);
    plen = rd32(d + 16);
    if (off < POE_HEADER_SIZE || plen == 0)
        return POE_BAD_HEADER;
    if (off > len || plen > len - off)
        return POE_TRUNCATED;
    if (!copy_text(out->name, d + 24, 32) || !copy_text(out->version, d + 56, 16) ||
        !copy_text(out->author, d + 72, 24))
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
