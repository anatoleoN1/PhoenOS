/*
 * PhoenOS - elf.c
 * Chargeur de programmes ELF statiques : 32 bits (i386) et 64 bits (x86-64, adresses sous 4 Gio). Tout champ du fichier est considere hostile.
 */
#include "elf.h"
#include "string.h"

#define EI_NIDENT 16
#define ET_EXEC   2
#define EM_386    3
#define EM_X86_64 62
#define PT_LOAD   1
#define MAX_PHDRS 16

struct elf_header {
    u8  ident[EI_NIDENT];
    u16 type, machine;
    u32 version, entry, phoff, shoff, flags;
    u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} __attribute__((packed));

struct elf_phdr {
    u32 type, offset, vaddr, paddr, filesz, memsz, flags, align;
} __attribute__((packed));

/* ELF 64 bits (programmes x86-64 de perf64) : memes champs, plus larges. Les adresses doivent tenir sur 32 bits. */
struct elf_header64 {
    u8  ident[EI_NIDENT];
    u16 type, machine;
    u32 version;
    u64 entry, phoff, shoff;
    u32 flags;
    u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} __attribute__((packed));

struct elf_phdr64 {
    u32 type, flags;
    u64 offset, vaddr, paddr, filesz, memsz, align;
} __attribute__((packed));

/* Un en-tete de programme, quelle que soit la classe. */
struct phd { u32 type, offset, vaddr, filesz, memsz, flags; };

int elf_class(const u8 *img, u32 size)
{
    if (!img || size < 20 || img[0] != 0x7F || img[1] != 'E' || img[2] != 'L' || img[3] != 'F' || img[5] != 1 || img[6] != 1)
        return 0;
    if (img[4] == 1 && size >= sizeof(struct elf_header) && (u32)(img[18] | img[19] << 8) == EM_386)
        return 32;
    if (img[4] == 2 && size >= sizeof(struct elf_header64) && (u32)(img[18] | img[19] << 8) == EM_X86_64)
        return 64;
    return 0;
}

/* Lit l'en-tete et le tableau des segments dans la forme commune. Tout depassement de 32 bits est refuse. */
static err_t elf_headers(const u8 *img, u32 size, struct phd *ph, u32 *nph, u32 *entry)
{
    int cls = elf_class(img, size);

    if (cls == 32) {
        struct elf_header h;

        memcpy(&h, img, sizeof(h));
        if (h.type != ET_EXEC || h.version != 1)
            return E_NOEXEC;
        if (h.phentsize != sizeof(struct elf_phdr) || h.phnum == 0 || h.phnum > MAX_PHDRS)
            return E_NOEXEC;
        if (h.phoff > size || (u32)h.phnum * sizeof(struct elf_phdr) > size - h.phoff)
            return E_NOEXEC;
        for (u32 i = 0; i < h.phnum; i++) {
            struct elf_phdr p;

            memcpy(&p, img + h.phoff + i * sizeof(p), sizeof(p));
            ph[i] = (struct phd){ p.type, p.offset, p.vaddr, p.filesz, p.memsz, p.flags };
        }
        *nph = h.phnum;
        *entry = h.entry;
        return E_OK;
    }
    if (cls == 64) {
        struct elf_header64 h;

        memcpy(&h, img, sizeof(h));
        if (h.type != ET_EXEC || h.version != 1)
            return E_NOEXEC;
        if (h.phentsize != sizeof(struct elf_phdr64) || h.phnum == 0 || h.phnum > MAX_PHDRS)
            return E_NOEXEC;
        if (h.phoff > size || (u64)h.phnum * sizeof(struct elf_phdr64) > size - h.phoff)
            return E_NOEXEC;
        if (h.entry >> 32)
            return E_NOEXEC;
        for (u32 i = 0; i < h.phnum; i++) {
            struct elf_phdr64 p;

            memcpy(&p, img + h.phoff + (u64)i * sizeof(p), sizeof(p));
            if (p.type == PT_LOAD && p.memsz != 0 &&
                ((p.vaddr >> 32) || (p.memsz >> 32) || (p.filesz >> 32) || (p.offset >> 32)))
                return E_NOEXEC;                                /* hors de portee (toute la zone des programmes est sous 4 Gio) */
            ph[i] = (struct phd){ p.type, (u32)p.offset, (u32)p.vaddr, (u32)p.filesz, (u32)p.memsz, p.flags };
        }
        *nph = h.phnum;
        *entry = (u32)h.entry;
        return E_OK;
    }
    return E_NOEXEC;
}

/* put == NULL : valide seulement (et remplit segs / nsegs si fournis). */
static err_t elf_walk(const u8 *img, u32 size, elf_put_fn put, void *pctx, u32 lo, u32 hi, u32 *entry,
                      struct elf_seg *segs, int *nsegs)
{
    struct phd ph[MAX_PHDRS];
    u32 nph = 0, ent = 0;
    int loaded = 0, entry_ok = 0;
    err_t he;

    if (!img || hi <= lo || size < sizeof(struct elf_header))
        return E_NOEXEC;
    he = elf_headers(img, size, ph, &nph, &ent);
    if (he != E_OK)
        return he;

    for (u32 i = 0; i < nph; i++) {
        struct phd p = ph[i];

        if (p.type != PT_LOAD || p.memsz == 0)
            continue;                                           /* segment vide (ld en ajoute parfois un) */
        if (p.filesz > p.memsz)
            return E_NOEXEC;
        if (p.vaddr < lo || p.vaddr - lo > hi - lo || p.memsz > hi - lo - (p.vaddr - lo))
            return E_NOEXEC;                                        /* hors de la zone permise */
        if (p.offset > size || p.filesz > size - p.offset)
            return E_NOEXEC;                                        /* segment hors du fichier */

        if (segs) {
            if (loaded >= ELF_MAX_SEGS)
                return E_NOEXEC;
            for (int k = 0; k < loaded; k++)
                if (p.vaddr < segs[k].hi && segs[k].lo < p.vaddr + p.memsz)
                    return E_NOEXEC;                            /* segments qui se chevauchent */
            segs[loaded].lo = p.vaddr;
            segs[loaded].hi = p.vaddr + p.memsz;
            segs[loaded].writable = (p.flags & 2) != 0;
        }
        if (put) {
            err_t pe = put(pctx, p.vaddr, img + p.offset, p.filesz);

            if (pe == E_OK && p.memsz > p.filesz)
                pe = put(pctx, p.vaddr + p.filesz, NULL, p.memsz - p.filesz);      /* .bss : zeros */
            if (pe != E_OK)
                return pe;
        }
        loaded++;
        if (ent >= p.vaddr && ent - p.vaddr < p.memsz)
            entry_ok = 1;
    }
    if (!loaded || !entry_ok)
        return E_NOEXEC;
    *entry = ent;
    if (nsegs)
        *nsegs = loaded;
    return E_OK;
}

err_t elf_segments(const u8 *img, u32 size, u32 lo, u32 hi, struct elf_seg *segs, int *nsegs, u32 *entry)
{
    return elf_walk(img, size, NULL, NULL, lo, hi, entry, segs, nsegs);
}

err_t elf_load_with(const u8 *img, u32 size, elf_put_fn put, void *pctx, u32 lo, u32 hi, u32 *entry)
{
    if (!put)
        return E_NOEXEC;
    return elf_walk(img, size, put, pctx, lo, hi, entry, NULL, NULL);
}

struct mem_ctx { u8 *mem; u32 lo; };

static err_t mem_put(void *c, u32 va, const u8 *src, u32 n)
{
    struct mem_ctx *m = c;

    if (src)
        memcpy(m->mem + (va - m->lo), src, n);
    else
        memset(m->mem + (va - m->lo), 0, n);
    return E_OK;
}

err_t elf_load(const u8 *img, u32 size, u8 *mem, u32 lo, u32 hi, u32 *entry)
{
    struct mem_ctx m = { mem, lo };

    if (!mem)
        return E_NOEXEC;
    return elf_walk(img, size, mem_put, &m, lo, hi, entry, NULL, NULL);
}
