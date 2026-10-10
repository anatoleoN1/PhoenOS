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

err_t elf_load(const u8 *img, u32 size, u8 *mem, u32 lo, u32 hi, u32 *entry)
{
    struct elf_header h;
    int loaded = 0, entry_ok = 0;

    if (!img || !mem || hi <= lo || size < sizeof(h))
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

        memcpy(mem + (p.vaddr - lo), img + p.offset, p.filesz);
        memset(mem + (p.vaddr - lo) + p.filesz, 0, p.memsz - p.filesz);
        loaded++;
        if (h.entry >= p.vaddr && h.entry - p.vaddr < p.memsz)
            entry_ok = 1;
    }
    if (!loaded || !entry_ok)
        return E_NOEXEC;
    *entry = h.entry;
    return E_OK;
}
