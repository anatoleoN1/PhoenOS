/*
 * Test du VRAI libmgr.c, psl.c, aspace.c (0.8.0) : bibliotheques partagees .psl.
 * Les bibliotheques sont de vrais ELF (lib/maths.c, lib/texte.c) lies par ld a leur adresse, emballes par le vrai
 * outil mkpsl. On verifie : le format, le partage du code (memes cadres), l'isolation des donnees, le demontage, les
 * conflits, la memoire pleine, et surtout on EXECUTE le code de la bibliotheque (copie dans une vraie zone a 0x700000).
 */
#include "testlib.h"
#include "multiboot.h"
#include "pmm.h"
#include "kheap.h"
#include "fs.h"
#include "paging.h"
#include "aspace.h"
#include "libmgr.h"
#include "psl.h"

u8 test_scancode;
char kernel_start[4096] __attribute__((aligned(4096)));
char kernel_end[1];
void test_panic(const char *fmt, ...) { out("  PANIQUE INATTENDUE : "); out(fmt); out("\n"); exit_now(2); }
void vga_putc(char c) { (void)c; }
void serial_putc(char c) { (void)c; }
u32 pit_uptime_ms(void) { return 0; }
u32 pit_ticks(void) { return 0; }
u32 pit_hz(void) { return 100; }

static char fake_ram[16u << 20] __attribute__((aligned(4096)));
static u32 kdir[1024] __attribute__((aligned(4096)));
static u32 ktabs[32 * 1024] __attribute__((aligned(4096)));
static u8 buf[1 << 17], work[1 << 17];

#define PT(a, va) ((a).pt[((va) - ASPACE_BASE) >> 12])

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

static long mmap_rwx(u32 addr, u32 len)
{
    long r;

    __asm__ volatile ("pushl %%ebp\n\tmovl $0, %%ebp\n\tint $0x80\n\tpopl %%ebp"
                      : "=a"(r) : "0"(192), "b"(addr), "c"(len), "d"(7), "S"(0x32), "D"(-1) : "memory", "cc");
    return r;
}

static void install(const char *host, const char *name)
{
    struct fs_node *f;
    char path[32] = "/lib/";
    long n = file(host, buf, sizeof buf);

    strcpy(path + 5, name);
    if (!(n > 0 && fs_create(path, &f) == E_OK && fs_write(f, buf, (u32)n) == E_OK)) { out("  install : "); out(host); out("\n"); test_fails++; }
}

/* Copie ce que le programme voit de la zone des bibliotheques dans la vraie memoire a 0x700000 (pour EXECUTER le code). */
static void view(const struct aspace *a)
{
    memset((void *)PSL_LIB_LO, 0, PSL_LIB_HI - PSL_LIB_LO);
    for (u32 va = PSL_LIB_LO; va < PSL_LIB_HI; va += 0x1000) {
        u32 e = a->pt[(va - ASPACE_BASE) >> 12];

        if (e & PAGE_P)
            memcpy((void *)va, (void *)(e & ~0xFFFu), 0x1000);
    }
}

/* ... et l'inverse pour une page de donnees (ce que le programme a ecrit). */
static void writeback(struct aspace *a, u32 va)
{
    u32 e = a->pt[(va - ASPACE_BASE) >> 12];

    memcpy((void *)(e & ~0xFFFu), (void *)va, 0x1000);
}

typedef u32 (*f0)(void);
typedef u32 (*f1)(u32);
typedef u32 (*f2)(u32, u32);
typedef void (*fs_)(char *);
typedef u32 (*fc)(const char *);

static u32 sym(struct uproc *p, const char *lib, const char *s)
{
    u32 a = 0;

    return libmgr_resolve(p, lib, s, &a) == E_OK ? a : 0;
}

int test_main(void)
{
    static struct { struct multiboot_info mbi; struct multiboot_mmap_entry e[1]; } M;
    static struct psl_info info;
    static struct uproc P1, P2, P3;
    static struct aspace A1, A2, A3;
    static u32 hold[8192];
    struct paging_cfg c = { 128u << 20, 0x100000, 0x115000, 0, 0, 0xA0000, 0xC0000 };
    u32 free0, a, n, nh;
    long len;
    const char *name;
    u32 refs, lo, hi, shared;

    CHECK(mmap_rwx(PSL_LIB_LO, PSL_LIB_HI - PSL_LIB_LO) == (long)PSL_LIB_LO);
    M.mbi.flags = MB_FLAG_MMAP;
    M.mbi.mmap_addr = (u32)M.e;
    M.mbi.mmap_length = sizeof M.e;
    M.e[0] = (struct multiboot_mmap_entry){ 20, (u32)fake_ram, 0, sizeof fake_ram, 0, MB_MMAP_AVAILABLE };
    pmm_init(&M.mbi);
    kheap_init();
    fs_init();
    CHECK(paging_build(kdir, ktabs, 32, &c) == E_OK);
    aspace_set_kernel_dir(kdir);
    fs_mkdir("/lib");
    install("lib/maths.psl", "maths.psl");
    install("lib/texte.psl", "texte.psl");
    install("lib/clash.psl", "clash.psl");
    install("lib/maths.psl", "mauvais.psl");              /* nom du fichier different de celui de l'en-tete */
    { struct fs_node *f; fs_lookup("/lib/mauvais.psl", &f); ((u8 *)f->data)[24] ^= 1; }

    /* ---- le format ---- */
    len = file("lib/maths.psl", buf, sizeof buf);
    CHECK(len > (long)PSL_HEADER_SIZE && psl_is(buf, (u32)len) && !psl_is(buf, 3));
    CHECK(psl_parse(buf, (u32)len, &info) == PSL_OK);
    CHECK(!strcmp(info.name, "maths") && !strcmp(info.author, "Equipe Test") && info.nexports == 5);
    CHECK(info.lo == 0x700000 && info.hi == 0x702000 && info.nseg == 2 && !info.segs[0].writable && info.segs[1].writable);
    CHECK(psl_find(&info, "maths_pgcd") == 0x700000 && psl_find(&info, "inconnu") == 0 && psl_find(&info, "maths_nom") != 0);
    CHECK(psl_parse(buf, 3, &info) == PSL_NOT_PSL && psl_parse(buf, 100, &info) == PSL_TRUNCATED);
    CHECK(psl_parse(buf, (u32)len - 1, &info) == PSL_TRUNCATED);
    memcpy(work, buf, (usize)len); work[(u32)len - 1] ^= 1;
    CHECK(psl_parse(work, (u32)len, &info) == PSL_BAD_CRC);
    memcpy(work, buf, (usize)len); work[4] = 2;
    CHECK(psl_parse(work, (u32)len, &info) == PSL_BAD_HEADER);                  /* format inconnu */
    memcpy(work, buf, (usize)len); work[8] = 99;
    CHECK(psl_parse(work, (u32)len, &info) == PSL_NEEDS_NEWER);
    memcpy(work, buf, (usize)len); work[128] = 17;
    CHECK(psl_parse(work, (u32)len, &info) == PSL_BAD_HEADER);                  /* 17 exports */
    memcpy(work, buf, (usize)len); work[136 + 16] = 0; work[136 + 17] = 0; work[136 + 18] = 0x50; work[136 + 19] = 0;
    CHECK(psl_parse(work, (u32)len, &info) == PSL_BAD_IMAGE);                   /* export hors de la bibliotheque */
    memcpy(work, buf, (usize)len); work[24] = 'M';
    CHECK(psl_parse(work, (u32)len, &info) == PSL_BAD_HEADER);                  /* nom invalide (majuscule) */
    memcpy(work, buf, (usize)len); memcpy(work + 136, "maths_racine", 13);      /* doublon d'export */
    CHECK(psl_parse(work, (u32)len, &info) == PSL_BAD_HEADER);
    {   /* 30000 en-tetes abimes au hasard : jamais de plantage, et si c'est accepte les exports sont dans la plage */
        u32 seed = 12345, ok = 0;

        for (int i = 0; i < 30000; i++) {
            memcpy(work, buf, PSL_HEADER_SIZE + 64);
            for (int k = 0; k < 1 + (int)(seed % 3); k++) {
                seed = seed * 1103515245u + 12345u;
                work[(seed >> 8) % PSL_HEADER_SIZE] = (u8)(seed >> 20);
            }
            seed = seed * 1103515245u + 12345u;
            if (psl_parse(work, (u32)len, &info) == PSL_OK) {
                ok++;
                for (int e = 0; e < info.nexports; e++)
                    if (info.exports[e].addr < info.lo || info.exports[e].addr >= info.hi) { out("  export hors plage accepte\n"); exit_now(2); }
            }
        }
        CHECK(ok < 30000);
    }
    CHECK(libmgr_info(0, &name, &refs, &lo, &hi, &shared) == 0);

    /* ---- partage : deux programmes, un seul exemplaire du code ---- */
    free0 = pmm_free_frames();
    CHECK(aspace_create(&A1) == E_OK && aspace_create(&A2) == E_OK && aspace_create(&A3) == E_OK);
    P1.as = &A1; P2.as = &A2; P3.as = &A3;
    strcpy(P1.name, "p1"); strcpy(P2.name, "p2"); strcpy(P3.name, "p3");
    n = pmm_free_frames();
    CHECK(libmgr_attach(&P1, "maths") == E_OK && P1.nlibs == 1);
    CHECK(n - pmm_free_frames() == 2);                                           /* 1 page de code (cache) + 1 page de donnees */
    CHECK(libmgr_attach(&P1, "maths") == E_OK && P1.nlibs == 1 && n - pmm_free_frames() == 2);   /* idempotent */
    CHECK(libmgr_attach(&P2, "maths") == E_OK && P2.nlibs == 1);
    CHECK(n - pmm_free_frames() == 3);                                           /* le code n'est PAS recopie : 1 page de donnees de plus */
    CHECK((PT(A1, 0x700000) & ~0xFFFu) == (PT(A2, 0x700000) & ~0xFFFu));                         /* meme cadre de code */
    CHECK((PT(A1, 0x701000) & ~0xFFFu) != (PT(A2, 0x701000) & ~0xFFFu));                         /* donnees : cadres differents */
    CHECK((PT(A1, 0x700000) & ASPACE_SHARED) && !(PT(A1, 0x700000) & PAGE_RW) && (PT(A1, 0x701000) & PAGE_RW) && !(PT(A1, 0x701000) & ASPACE_SHARED));
    CHECK(aspace_check(&A1, 0x700000, 16, 0) && !aspace_check(&A1, 0x700000, 1, 1) && aspace_check(&A1, 0x701000, 4, 1));
    CHECK(aspace_put(&A1, 0x700010, (const u8 *)"x", 1) == E_FAULT);                 /* jamais d'ecriture dans le code partage */
    CHECK(aspace_map(&A1, 0x700000, 0x701000, 1) == E_OK && !(PT(A1, 0x700000) & PAGE_RW));   /* meme "mapper en ecriture" ne l'ouvre pas */
    CHECK(aspace_protect(&A1, 0x700000, 0x701000, 1) == E_OK && !(PT(A1, 0x700000) & PAGE_RW));
    CHECK(libmgr_info(0, &name, &refs, &lo, &hi, &shared) && !strcmp(name, "maths") && refs == 2 && lo == 0x700000 && hi == 0x702000 && shared == 1);
    CHECK(libmgr_info(1, &name, &refs, &lo, &hi, &shared) == 0);

    /* ---- on EXECUTE la bibliotheque ---- */
    CHECK(libmgr_resolve(&P1, "maths", NULL, &a) == E_OK && a == 0x700000);
    CHECK(libmgr_resolve(&P1, "maths", "inconnu", &a) == E_NOENT);
    CHECK(libmgr_resolve(&P1, "absente", "x", &a) == E_NOLIB);
    view(&A1);
    CHECK(((f2)sym(&P1, "maths", "maths_pgcd"))(84, 36) == 12 && ((f2)sym(&P1, "maths", "maths_pgcd"))(17, 5) == 1);
    CHECK(((f1)sym(&P1, "maths", "maths_racine"))(1000000) == 1000 && ((f1)sym(&P1, "maths", "maths_fibo"))(30) == 832040);
    CHECK(((f0)sym(&P1, "maths", "maths_appels"))() == 4);                       /* 4 appels faits dans CE programme */
    CHECK(!strcmp((const char *)sym(&P1, "maths", "maths_nom"), "maths 1.0"));
    writeback(&A1, 0x701000);                                                    /* le programme 1 a modifie ses donnees */
    view(&A2);
    CHECK(((f0)sym(&P2, "maths", "maths_appels"))() == 0);                       /* le programme 2 ne voit PAS ces changements */
    CHECK(((f2)sym(&P2, "maths", "maths_pgcd"))(100, 75) == 25 && ((f0)sym(&P2, "maths", "maths_appels"))() == 1);
    writeback(&A2, 0x701000);
    view(&A1);
    CHECK(((f0)sym(&P1, "maths", "maths_appels"))() == 4);                       /* et le 1 garde les siennes */

    /* ---- une deuxieme bibliotheque ---- */
    n = pmm_free_frames();
    CHECK(libmgr_attach(&P1, "texte") == E_OK && P1.nlibs == 2 && n - pmm_free_frames() == 1);   /* texte : du code seul, pas de donnees */
    view(&A1);
    {
        static char s[40] = "bonjour le monde";

        CHECK(((fc)sym(&P1, "texte", "texte_mots"))(s) == 3 && ((fc)sym(&P1, "texte", "texte_longueur"))(s) == 16);
        ((fs_)sym(&P1, "texte", "texte_maj"))(s);
        CHECK(!strcmp(s, "BONJOUR LE MONDE"));
        ((fs_)sym(&P1, "texte", "texte_inverser"))(s);
        CHECK(!strcmp(s, "EDNOM EL RUOJNOB"));
    }
    CHECK(((f2)sym(&P1, "maths", "maths_pgcd"))(48, 18) == 6);                    /* les deux bibliotheques cohabitent */

    /* ---- conflits et mauvais fichiers ---- */
    n = pmm_free_frames();
    a = (u32)A1.pages;
    CHECK(libmgr_attach(&P1, "clash") == E_BUSY && P1.nlibs == 2 && n == pmm_free_frames() && A1.pages == a);   /* plage deja prise */
    CHECK(libmgr_attach(&P3, "clash") == E_OK && P3.nlibs == 1);                  /* seule, elle va tres bien */
    libmgr_detach_all(&P3);
    aspace_destroy(&A3);
    n = pmm_free_frames();
    CHECK(libmgr_attach(&P1, "absente") == E_NOLIB && libmgr_attach(&P1, "../etc") == E_NOLIB && libmgr_attach(&P1, "") == E_NOLIB);
    CHECK(libmgr_attach(&P1, "mauvais") == E_NOLIB);                              /* fichier abime */
    CHECK(P1.nlibs == 2 && n == pmm_free_frames());

    /* ---- demontage ---- */
    libmgr_detach_all(&P1);
    aspace_destroy(&A1);
    CHECK(P1.nlibs == 0);
    CHECK(libmgr_info(0, &name, &refs, &lo, &hi, &shared) && !strcmp(name, "maths") && refs == 1);   /* P2 l'utilise encore */
    CHECK(libmgr_info(1, &name, &refs, &lo, &hi, &shared) == 0);                  /* texte n'est plus utilisee : rendue */
    view(&A2);
    CHECK(((f2)sym(&P2, "maths", "maths_pgcd"))(9, 6) == 3);                      /* le code est toujours la pour P2 */
    libmgr_detach_all(&P2);
    aspace_destroy(&A2);
    CHECK(libmgr_info(0, &name, &refs, &lo, &hi, &shared) == 0);
    CHECK(pmm_free_frames() == free0);                                            /* AUCUNE fuite */

    /* ---- memoire pleine ---- */
    CHECK(aspace_create(&A1) == E_OK);
    memset(&P1, 0, sizeof P1); P1.as = &A1;
    nh = 0;
    while (nh < 8192 && (hold[nh] = pmm_alloc_frame()) != 0)
        nh++;
    CHECK(pmm_free_frames() == 0);
    CHECK(libmgr_attach(&P1, "maths") == E_NOMEM && P1.nlibs == 0);
    for (u32 i = 0; i < nh; i++)
        pmm_free_frame(hold[i]);
    /* quelques frames seulement : le code passe, les donnees non */
    nh = 0;
    while (pmm_free_frames() > 1)
        hold[nh++] = pmm_alloc_frame();
    CHECK(libmgr_attach(&P1, "maths") == E_NOMEM && P1.nlibs == 0 && A1.pages == 0);
    for (u32 i = 0; i < nh; i++)
        pmm_free_frame(hold[i]);
    CHECK(libmgr_info(0, &name, &refs, &lo, &hi, &shared) == 0);                  /* rien ne reste en cache apres l'echec */
    CHECK(libmgr_attach(&P1, "maths") == E_OK);                                   /* et ca remarche des qu'il y a de la place */
    libmgr_detach_all(&P1);
    aspace_destroy(&A1);
    CHECK(pmm_free_frames() == free0);

    out("  bibliotheques verifiees : format, 30000 en-tetes abimes, code partage entre programmes, donnees privees, code EXECUTE, conflits, memoire pleine, aucune fuite\n");
    return 0;
}
