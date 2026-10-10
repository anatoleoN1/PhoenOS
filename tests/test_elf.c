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
    struct elf_seg segs[ELF_MAX_SEGS];
    int ns = 0;
    err_t r = elf_load(img, sz, mem, LO, HI, &e), r2 = elf_segments(img, sz, LO, HI, segs, &ns, &e);

    if ((r == E_OK) != (r2 == E_OK))
        test_fails++, out("  ECHEC : elf_load et elf_segments ne sont pas d'accord\n");
    return r;
}

static err_t put_cb(void *c, u32 va, const u8 *src, u32 n)
{
    u8 *m = *(u8 **)c;

    if (va < LO || va - LO + n > HI - LO)
        return E_FAULT;
    if (src) memcpy(m + (va - LO), src, n); else memset(m + (va - LO), 0, n);
    return E_OK;
}

int test_main(void)
{
    static const char *const progs[] = { "hello", "primes", "saisie", "compte", "faute", "heure", "calc", "ticker", "boucle" };
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
        {
            static u8 mem2[sizeof mem];
            struct pc { u8 *m; } pcx = { mem2 };
            u32 e3 = 0;

            memset(mem2, 0xEE, sizeof mem2);
            CHECK(elf_load_with(img, (u32)n, put_cb, &pcx, LO, HI, &e3) == E_OK && e3 == entry);
            CHECK(memcmp(mem, mem2, sizeof mem) == 0);                  /* meme resultat que elf_load */
        }
        {
            struct elf_seg segs[ELF_MAX_SEGS];
            int ns = 0;
            u32 e2 = 0;

            CHECK(elf_segments(img, (u32)n, LO, HI, segs, &ns, &e2) == E_OK && e2 == entry);
            CHECK(ns >= 1 && ns <= 2 && !segs[0].writable && entry >= segs[0].lo && entry < segs[0].hi);   /* le code est en lecture seule */
            CHECK(ns == 1 || (segs[1].writable && segs[1].lo >= segs[0].hi && segs[1].lo % 4096 == 0));    /* donnees : page a part */
            CHECK(segs[ns - 1].hi <= HI && segs[0].lo >= LO);
        }
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


    /* ---- segments qui se chevauchent / trop nombreux : refuses par elf_segments (et jamais charges) ---- */
    {
        struct elf_seg segs[ELF_MAX_SEGS];
        struct ph p2;
        int ns = 0;
        u32 en;

        forge(LO + 0x1000, 256, 256, 4096, LO + 0x1000);
        img[44] = 2;                                                            /* phnum = 2 */
        memset(&p2, 0, sizeof p2);
        p2.type = 1; p2.offset = 4096; p2.vaddr = LO + 0x1080; p2.paddr = p2.vaddr; p2.filesz = 64; p2.memsz = 64; p2.flags = 6;
        memcpy(img + 52 + 32, &p2, sizeof p2);
        CHECK(elf_segments(img, 4352, LO, HI, segs, &ns, &en) == E_NOEXEC);     /* chevauchement */
        p2.vaddr = LO + 0x2000;
        memcpy(img + 52 + 32, &p2, sizeof p2);
        CHECK(elf_segments(img, 4352, LO, HI, segs, &ns, &en) == E_OK && ns == 2 && segs[1].writable && !segs[0].writable);
        CHECK(segs[0].lo == LO + 0x1000 && segs[0].hi == LO + 0x1100 && segs[1].lo == LO + 0x2000 && segs[1].hi == LO + 0x2040);
        forge(LO + 0x1000, 256, 256, 4096, LO + 0x1000);
        img[44] = 9;                                                            /* 9 segments : plus que ELF_MAX_SEGS */
        for (int i = 1; i < 9; i++) {
            p2.vaddr = LO + 0x2000u + 0x1000u * (u32)i;
            memcpy(img + 52 + 32 * (usize)i, &p2, sizeof p2);
        }
        CHECK(elf_segments(img, 4352, LO, HI, segs, &ns, &en) == E_NOEXEC);
    }

    out("  chargeur ELF verifie : 11 vrais programmes (dont des ELF a deux segments), 13 fichiers hostiles, en-tetes corrompus\n");
    return 0;
}
