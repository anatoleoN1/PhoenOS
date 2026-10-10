/*
 * PhoenOS - elf.c
 * Chargeur de programmes ELF32 statiques. Tout champ du fichier est considere hostile.
 */
#include "elf.h"
#include "string.h"

#define EI_NIDENT 16
#define ET_EXEC   2
#define EM_386    3
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

/* put == NULL : valide seulement (et remplit segs / nsegs si fournis). */
static err_t elf_walk(const u8 *img, u32 size, elf_put_fn put, void *pctx, u32 lo, u32 hi, u32 *entry,
                      struct elf_seg *segs, int *nsegs)
{
    struct elf_header h;
    int loaded = 0, entry_ok = 0;

    if (!img || hi <= lo || size < sizeof(h))
        return E_NOEXEC;
    memcpy(&h, img, sizeof(h));

    if (h.ident[0] != 0x7F || h.ident[1] != 'E' || h.ident[2] != 'L' || h.ident[3] != 'F')
        return E_NOEXEC;
    if (h.ident[4] != 1 || h.ident[5] != 1 || h.ident[6] != 1)      /* 32 bits, petit-boutiste, v1 */
        return E_NOEXEC;
    if (h.type != ET_EXEC || h.machine != EM_386 || h.version != 1)
        return E_NOEXEC;
    if (h.phentsize != sizeof(struct elf_phdr) || h.phnum == 0 || h.phnum > MAX_PHDRS)
        return E_NOEXEC;
    if (h.phoff > size || (u32)h.phnum * sizeof(struct elf_phdr) > size - h.phoff)
        return E_NOEXEC;

    for (u32 i = 0; i < h.phnum; i++) {
        struct elf_phdr p;

        memcpy(&p, img + h.phoff + i * sizeof(p), sizeof(p));
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
        if (h.entry >= p.vaddr && h.entry - p.vaddr < p.memsz)
            entry_ok = 1;
    }
    if (!loaded || !entry_ok)
        return E_NOEXEC;
    *entry = h.entry;
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
