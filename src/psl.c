/* PhoenOS - psl.c : lecture, verification et creation des bibliotheques .psl (voir psl.h). */
#include "psl.h"
#include "poe.h"
#include "crc32.h"
#include "string.h"

static u32 rd32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static u32 rd16(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8; }
static void wr16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void wr32(u8 *p, u32 v) { wr16(p, v & 0xFFFF); wr16(p + 2, v >> 16); }

int psl_is(const u8 *d, u32 len)
{
    return len >= 4 && d[0] == 'P' && d[1] == 'S' && d[2] == 'L' && d[3] == 1;
}

int psl_valid_symname(const char *s)
{
    usize n = strlen(s);

    if (n == 0 || n >= PSL_SYM_MAX)
        return 0;
    for (usize i = 0; i < n; i++) {
        char c = s[i];

        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
            return 0;
    }
    return 1;
}

/* Texte de l'en-tete : ASCII imprimable, 0 final dans la case, rien apres ; vide accepte seulement si opt. */
static int text(char *dst, const u8 *src, u32 size, int opt)
{
    u32 i;

    for (i = 0; i < size && src[i]; i++) {
        if (src[i] < 32 || src[i] > 126)
            return 0;
        dst[i] = (char)src[i];
    }
    if (i == size || (i == 0 && !opt))
        return 0;
    for (u32 k = i; k < size; k++)
        if (src[k])
            return 0;
    dst[i] = 0;
    return 1;
}

static u32 page_dn(u32 v) { return v & ~0xFFFu; }
static u32 page_up(u32 v) { return (v + 0xFFFu) & ~0xFFFu; }

/* L'ELF est-il une bibliotheque valable ? Remplit lo, hi, segs. */
static int check_image(struct psl_info *o)
{
    u32 entry;
    u32 ro_pages = 0;

    if (o->payload_len > PSL_MAX_IMAGE)
        return 0;
    o->cls = elf_class(o->payload, o->payload_len);
    if (!o->cls)
        return 0;
    if (elf_segments(o->payload, o->payload_len, PSL_LIB_LO, PSL_LIB_HI, o->segs, &o->nseg, &entry) != E_OK)
        return 0;
    o->lo = 0xFFFFFFFFu;
    o->hi = 0;
    for (int i = 0; i < o->nseg; i++) {
        u32 a = page_dn(o->segs[i].lo), b = page_up(o->segs[i].hi);

        if (a < o->lo) o->lo = a;
        if (b > o->hi) o->hi = b;
        if (!o->segs[i].writable)
            ro_pages += (b - a) >> 12;
        for (int k = 0; k < i; k++) {                   /* deux segments ne partagent jamais une page */
            u32 c = page_dn(o->segs[k].lo), d = page_up(o->segs[k].hi);

            if (a < d && c < b)
                return 0;
        }
    }
    return o->nseg > 0 && ro_pages <= PSL_MAX_PAGES;
}

enum psl_status psl_parse(const u8 *d, u32 len, struct psl_info *out)
{
    u32 off, plen, n;

    memset(out, 0, sizeof(*out));
    if (!psl_is(d, len))
        return PSL_NOT_PSL;
    if (len < PSL_HEADER_SIZE)
        return PSL_TRUNCATED;
    if (rd16(d + 4) != PSL_FORMAT || rd16(d + 6) != 0 || rd32(d + 132) != 0)
        return PSL_BAD_HEADER;
    off = rd32(d + 12);
    plen = rd32(d + 16);
    if (off < PSL_HEADER_SIZE || plen == 0)
        return PSL_BAD_HEADER;
    if (off > len || plen > len - off)
        return PSL_TRUNCATED;
    if (!text(out->name, d + 24, 16, 0) || !text(out->version, d + 40, 16, 0) || !text(out->author, d + 56, 24, 0) ||
        !text(out->description, d + 80, 48, 1) || !poe_valid_libname(out->name))
        return PSL_BAD_HEADER;
    n = rd32(d + 128);
    if (n > PSL_MAX_EXPORTS)
        return PSL_BAD_HEADER;
    for (u32 i = 0; i < n; i++) {
        const u8 *e = d + 136 + i * 20;

        if (!text(out->exports[i].name, e, PSL_SYM_MAX, 0) || !psl_valid_symname(out->exports[i].name))
            return PSL_BAD_HEADER;
        for (u32 k = 0; k < i; k++)
            if (!strcmp(out->exports[k].name, out->exports[i].name))
                return PSL_BAD_HEADER;
        out->exports[i].addr = rd32(e + 16);
    }
    out->nexports = (int)n;
    out->min_api = rd32(d + 8);
    out->payload = d + off;
    out->payload_len = plen;
    if (crc32_update(0, out->payload, plen) != rd32(d + 20))
        return PSL_BAD_CRC;
    if (out->min_api > POE_API_LEVEL)
        return PSL_NEEDS_NEWER;
    if (!check_image(out))
        return PSL_BAD_IMAGE;
    for (int i = 0; i < out->nexports; i++)
        if (out->exports[i].addr < out->lo || out->exports[i].addr >= out->hi)
            return PSL_BAD_IMAGE;                       /* exporte autre chose que ce que la bibliotheque contient */
    return PSL_OK;
}

const char *psl_status_text(enum psl_status s)
{
    switch (s) {
    case PSL_OK:          return "valide";
    case PSL_NOT_PSL:     return "ce n'est pas une bibliotheque .psl";
    case PSL_TRUNCATED:   return "fichier .psl tronque";
    case PSL_BAD_HEADER:  return "en-tete .psl invalide";
    case PSL_BAD_CRC:     return "bibliotheque abimee (somme de controle incorrecte)";
    case PSL_NEEDS_NEWER: return "exige une version plus recente de PhoenOS";
    case PSL_BAD_IMAGE:   return "contenu invalide (ELF hors de la zone des bibliotheques, segments qui se partagent une page, export hors plage)";
    }
    return "erreur inconnue";
}

u32 psl_find(const struct psl_info *info, const char *sym)
{
    for (int i = 0; i < info->nexports; i++)
        if (!strcmp(info->exports[i].name, sym))
            return info->exports[i].addr;
    return 0;
}

struct elf_hdr32 {
    u8  ident[16];
    u16 type, machine;
    u32 version, entry, phoff, shoff, flags;
    u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} __attribute__((packed));
struct elf_sh32 { u32 name, type, flags, addr, offset, size, link, info, addralign, entsize; } __attribute__((packed));
struct elf_sym32 { u32 name, value, size; u8 info, other; u16 shndx; } __attribute__((packed));

struct elf_hdr64 {
    u8  ident[16];
    u16 type, machine;
    u32 version;
    u64 entry, phoff, shoff;
    u32 flags;
    u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} __attribute__((packed));
struct elf_sh64 { u32 name, type; u64 flags, addr, offset, size; u32 link, info; u64 addralign, entsize; } __attribute__((packed));
struct elf_sym64 { u32 name; u8 info, other; u16 shndx; u64 value, size; } __attribute__((packed));

/* Forme commune d'un en-tete de section et d'un symbole, quelle que soit la classe. */
static int sym_search(const u8 *elf, u32 len, const char *want, u32 *addr, int cls)
{
    u64 shoff;
    u32 shnum, shentsize, shsz = cls == 64 ? (u32)sizeof(struct elf_sh64) : (u32)sizeof(struct elf_sh32);
    u32 symsz = cls == 64 ? (u32)sizeof(struct elf_sym64) : (u32)sizeof(struct elf_sym32);

    if (cls == 64) {
        struct elf_hdr64 h;

        if (len < sizeof h)
            return 0;
        memcpy(&h, elf, sizeof h);
        shoff = h.shoff; shnum = h.shnum; shentsize = h.shentsize;
    } else {
        struct elf_hdr32 h;

        if (len < sizeof h)
            return 0;
        memcpy(&h, elf, sizeof h);
        shoff = h.shoff; shnum = h.shnum; shentsize = h.shentsize;
    }
    if (shentsize != shsz || shnum == 0 || shoff > len || (u64)shnum * shsz > len - shoff)
        return 0;
    for (u32 i = 0; i < shnum; i++) {
        u32 type, link;
        u64 off, size, entsize, soff, ssize;
        usize n = strlen(want);

        if (cls == 64) {
            struct elf_sh64 sh, str;

            memcpy(&sh, elf + shoff + (u64)i * shsz, shsz);
            type = sh.type; link = sh.link; off = sh.offset; size = sh.size; entsize = sh.entsize;
            if (type != 2 || entsize != symsz || link >= shnum)
                continue;
            memcpy(&str, elf + shoff + (u64)link * shsz, shsz);
            soff = str.offset; ssize = str.size;
        } else {
            struct elf_sh32 sh, str;

            memcpy(&sh, elf + shoff + (u64)i * shsz, shsz);
            type = sh.type; link = sh.link; off = sh.offset; size = sh.size; entsize = sh.entsize;
            if (type != 2 || entsize != symsz || link >= shnum)     /* SHT_SYMTAB */
                continue;
            memcpy(&str, elf + shoff + (u64)link * shsz, shsz);
            soff = str.offset; ssize = str.size;
        }
        if (off > len || size > len - off || soff > len || ssize > len - soff || ssize == 0)
            return 0;
        for (u32 k = 0; k < (u32)size / symsz; k++) {
            u32 nm, j;
            u8 info;
            u16 shndx;
            u64 value;

            if (cls == 64) {
                struct elf_sym64 sy;

                memcpy(&sy, elf + off + (u64)k * symsz, symsz);
                nm = sy.name; info = sy.info; shndx = sy.shndx; value = sy.value;
            } else {
                struct elf_sym32 sy;

                memcpy(&sy, elf + off + (u64)k * symsz, symsz);
                nm = sy.name; info = sy.info; shndx = sy.shndx; value = sy.value;
            }
            if (shndx == 0 || shndx >= 0xFF00u || ((info >> 4) != 1 && (info >> 4) != 2))   /* global ou faible, defini */
                continue;
            if (nm >= ssize || n >= ssize - nm)
                continue;
            for (j = 0; j < n && elf[soff + nm + j] == (u8)want[j]; j++)
                ;
            if (j == n && elf[soff + nm + n] == 0) {
                if (value >> 32)
                    return 0;
                *addr = (u32)value;
                return 1;
            }
        }
        return 0;
    }
    return 0;
}

int psl_elf_symbol(const u8 *elf, u32 len, const char *want, u32 *addr)
{
    int cls = elf_class(elf, len);

    return cls ? sym_search(elf, len, want, addr, cls) : 0;
}

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

u32 psl_build(const struct psl_spec *s, const u8 *elf, u32 elf_len, u8 *out, u32 cap, const char **why)
{
    static struct psl_info chk;                 /* gros : pas sur la pile */
    const char *dummy;
    enum psl_status st;
    u32 a;

    if (!why)
        why = &dummy;
    if (s->nexports < 0 || s->nexports > PSL_MAX_EXPORTS) { *why = "trop d'exports (16 au plus)"; return 0; }
    if (!elf || elf_len < 52 || elf[0] != 0x7F || elf[1] != 'E' || elf[2] != 'L' || elf[3] != 'F') {
        *why = "le fichier n'est pas un ELF";
        return 0;
    }
    if (elf_len > PSL_MAX_IMAGE || PSL_HEADER_SIZE + elf_len > cap) { *why = "bibliotheque trop grosse"; return 0; }
    memset(out, 0, PSL_HEADER_SIZE);
    memcpy(out, "PSL\1", 4);
    wr16(out + 4, PSL_FORMAT);
    wr32(out + 8, s->min_api ? s->min_api : 2u);
    wr32(out + 12, PSL_HEADER_SIZE);
    wr32(out + 16, elf_len);
    wr32(out + 20, crc32_update(0, elf, elf_len));
    if (!s->name || !poe_valid_libname(s->name)) { *why = "nom : 1 a 15 caracteres parmi a-z 0-9 _ -"; return 0; }
    put_text(out + 24, 16, s->name, 0);
    if (!put_text(out + 40, 16, s->version, 0)) { *why = "version : 1 a 15 caracteres ASCII imprimables"; return 0; }
    if (!put_text(out + 56, 24, s->author, 0)) { *why = "auteur : 1 a 23 caracteres ASCII imprimables"; return 0; }
    if (!put_text(out + 80, 48, s->description, 1)) { *why = "description : 47 caracteres au plus"; return 0; }
    wr32(out + 128, (u32)s->nexports);
    for (int i = 0; i < s->nexports; i++) {
        if (!s->exports[i].name || !psl_valid_symname(s->exports[i].name)) { *why = "nom d'export invalide (A-Z a-z 0-9 _, 15 au plus)"; return 0; }
        put_text(out + 136 + i * 20, PSL_SYM_MAX, s->exports[i].name, 0);
        a = s->exports[i].addr;
        if (!a && !psl_elf_symbol(elf, elf_len, s->exports[i].name, &a)) { *why = "symbole a exporter introuvable dans l'ELF (strip ?)"; return 0; }
        wr32(out + 136 + i * 20 + 16, a);
    }
    memcpy(out + PSL_HEADER_SIZE, elf, elf_len);
    st = psl_parse(out, PSL_HEADER_SIZE + elf_len, &chk);
    if (st != PSL_OK) {
        *why = psl_status_text(st);
        return 0;
    }
    return PSL_HEADER_SIZE + elf_len;
}
