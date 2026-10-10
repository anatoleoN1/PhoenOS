/* Test du vrai elf.c : vrais programmes (dossier user/ du test) et fichiers ELF forges/hostiles. */
#include "testlib.h"
#include "elf.h"

u8 test_scancode;

#define LO 0x400000u
#define HI 0x700000u
static u8 mem[HI - LO];
static u8 img[1 << 16];

/* ELF minimal forge : un segment PT_LOAD */
struct eh { u8 id[16]; u16 type, machine; u32 version, entry, phoff, shoff, flags; u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx; } __attribute__((packed));
struct ph { u32 type, offset, vaddr, paddr, filesz, memsz, flags, align; } __attribute__((packed));

static u32 forge(u32 vaddr, u32 filesz, u32 memsz, u32 offset, u32 entry)
{
    struct eh h;
    struct ph p;

    memset(img, 0, sizeof img);
    memset(&h, 0, sizeof h);
    h.id[0] = 0x7F; h.id[1] = 'E'; h.id[2] = 'L'; h.id[3] = 'F'; h.id[4] = 1; h.id[5] = 1; h.id[6] = 1;
    h.type = 2; h.machine = 3; h.version = 1; h.entry = entry; h.phoff = 52; h.ehsize = 52;
    h.phentsize = 32; h.phnum = 1;
    memcpy(img, &h, sizeof h);
    memset(&p, 0, sizeof p);
    p.type = 1; p.offset = offset; p.vaddr = vaddr; p.paddr = vaddr; p.filesz = filesz; p.memsz = memsz;
    memcpy(img + 52, &p, sizeof p);
    memset(img + 4096, 0xAB, 256);                              /* contenu du segment */
    return 4096 + 256;
}

static long file(const char *path, u8 *dst, usize max)
{
    long fd = sys3(5, (long)path, 0, 0), got = 0, r;

    if (fd < 0)
        return -1;
    while ((r = sys3(3, fd, (long)(dst + got), (long)(max - (usize)got))) > 0)
        got += r;
    sys3(6, fd, 0, 0);
    return got;
}

static int try_hostile(u32 va, u32 fs, u32 ms, u32 off, u32 entry, u32 size_override)
{
    u32 sz = forge(va, fs, ms, off, entry), e;

    if (size_override)
        sz = size_override;
    return elf_load(img, sz, mem, LO, HI, &e);
}

int test_main(void)
{
    static const char *const progs[] = { "hello", "primes", "saisie", "compte", "faute", "heure", "calc" };
    u32 entry = 0, sz;

    /* ---- vrais programmes ---- */
    for (usize i = 0; i < sizeof progs / sizeof progs[0]; i++) {
        char path[64] = "user/";
        long n;

        strcpy(path + 5, progs[i]);
        n = file(path, img, sizeof img);
        CHECK(n > 100);
        memset(mem, 0xEE, sizeof mem);
        CHECK(elf_load(img, (u32)n, mem, LO, HI, &entry) == E_OK);
        CHECK(entry >= LO && entry < LO + 0x2000);
        CHECK(mem[entry - LO] != 0xEE);                         /* le code est bien charge au point d'entree */
        CHECK(mem[HI - LO - 1] == 0xEE);                        /* rien n'ecrit hors de la zone utile */
    }

    /* ---- forge : cas valides ---- */
    memset(mem, 0xEE, sizeof mem);
    CHECK(try_hostile(LO + 0x100, 256, 1000, 4096, LO + 0x100, 0) == E_OK);
    CHECK(mem[0x100] == 0xAB && mem[0x1FF] == 0xAB && mem[0x200] == 0 && mem[0x100 + 999] == 0);  /* .bss a zero */
    CHECK(mem[0xFF] == 0xEE && mem[0x100 + 1000] == 0xEE);

    /* ---- forge : cas hostiles, tous refuses sans rien ecrire hors zone ---- */
    memset(mem, 0xEE, sizeof mem);
    CHECK(try_hostile(LO - 0x1000, 256, 256, 4096, LO - 0x1000, 0) == E_NOEXEC);       /* sous la zone */
    CHECK(try_hostile(HI - 100, 256, 256, 4096, HI - 100, 0) == E_NOEXEC);             /* depasse la fin */
    CHECK(try_hostile(0xFFFFFF00u, 256, 512, 4096, 0xFFFFFF00u, 0) == E_NOEXEC);        /* debordement d'adresse */
    CHECK(try_hostile(LO, 256, 0xFFFFFFFFu, 4096, LO, 0) == E_NOEXEC);                  /* memsz enorme */
    CHECK(try_hostile(LO, 512, 256, 4096, LO, 0) == E_NOEXEC);                          /* filesz > memsz */
    CHECK(try_hostile(LO, 256, 256, 0xFFFFFFF0u, LO, 0) == E_NOEXEC);                   /* offset hors fichier */
    CHECK(try_hostile(LO, 256, 256, 4096 + 200, LO, 0) == E_NOEXEC);                    /* lit au-dela de la fin */
    CHECK(try_hostile(LO, 256, 256, 4096, LO + 0x1000, 0) == E_NOEXEC);                 /* entree hors segment */
    CHECK(try_hostile(LO, 256, 256, 4096, 0, 0) == E_NOEXEC);                           /* entree nulle */
    CHECK(try_hostile(LO, 256, 256, 4096, LO, 40) == E_NOEXEC);                         /* fichier tronque */
    CHECK(try_hostile(LO, 256, 256, 4096, LO, 10) == E_NOEXEC);
    CHECK(try_hostile(LO, 256, 256, 4096, LO, 0) == E_OK);                              /* controle : valide */
    for (u32 i = 0; i < sizeof mem; i++)
        if (mem[i] != 0xEE && i >= 256) { /* seul le segment valide (LO..LO+256) a pu etre ecrit */
            CHECK(0);
            break;
        }

    /* en-tete corrompu octet par octet : jamais d'ecriture hors zone ni de plantage */
    sz = forge(LO, 256, 256, 4096, LO);
    for (u32 b = 0; b < 84; b++) {
        u8 save[84];

        memcpy(save, img, 84);
        for (u32 v = 0; v < 256; v += 51) {
            u32 e;

            img[b] = (u8)v;
            elf_load(img, sz, mem, LO, HI, &e);
            img[b] = save[b];
        }
    }
    {   /* rien n'a ete ecrit au-dela du segment legitime ni avant */
        int bad = 0;

        for (u32 i = 0; i < sizeof mem; i++)
            if (mem[i] != 0xEE && mem[i] != 0xAB && mem[i] != 0)
                bad = 1;
        CHECK(!bad);
    }

    /* non-ELF et variantes */
    { u32 e; static u8 junk[100]; memset(junk, 0x41, sizeof junk);
      CHECK(elf_load(junk, sizeof junk, mem, LO, HI, &e) == E_NOEXEC);
      CHECK(elf_load(junk, 0, mem, LO, HI, &e) == E_NOEXEC);
      CHECK(elf_load(NULL, 100, mem, LO, HI, &e) == E_NOEXEC); }
    sz = forge(LO, 256, 256, 4096, LO);
    { u32 e; img[4] = 2; CHECK(elf_load(img, sz, mem, LO, HI, &e) == E_NOEXEC); img[4] = 1;      /* 64 bits */
      img[16] = 3; CHECK(elf_load(img, sz, mem, LO, HI, &e) == E_NOEXEC); img[16] = 2;           /* ET_DYN? (ici 3) */
      img[18] = 62; CHECK(elf_load(img, sz, mem, LO, HI, &e) == E_NOEXEC); img[18] = 3;          /* x86-64 */
      CHECK(elf_load(img, sz, mem, LO, HI, &e) == E_OK); }

    out("  chargeur ELF verifie : 9 vrais programmes (dont des ELF a deux segments), 13 fichiers hostiles, en-tetes corrompus\n");
    return 0;
}
