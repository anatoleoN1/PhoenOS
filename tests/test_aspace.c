/* Test du vrai aspace.c : un espace d'adressage par programme (isolation, droits, memoire pleine, fuites). */
#include "testlib.h"
#include "paging.h"
#include "aspace.h"
#include "pmm.h"
#include "multiboot.h"

u8 test_scancode;
char kernel_start[4096] __attribute__((aligned(4096)));
char kernel_end[1];
void test_panic(const char *fmt, ...) { out("  PANIQUE INATTENDUE : "); out(fmt); out("\n"); exit_now(2); }
static char fake_ram[8u << 20] __attribute__((aligned(4096)));

#define TOP (128u << 20)
static u32 dir[1024] __attribute__((aligned(4096)));
static u32 tabs[32 * 1024] __attribute__((aligned(4096)));

static int act_user, act_kernel;
void aspace_activate(const struct aspace *a) { (void)a; act_user++; }
void aspace_activate_kernel(void) { act_kernel++; }

int test_main(void)
{
    struct paging_cfg c = { TOP, 0x100000, 0x115000, 0, 0, 0xA0000, 0xC0000 };
    u32 pa = 0;

    /* ---- espaces d'adressage des programmes (0.5.0) ---- */
    {
        static struct { struct multiboot_info mbi; struct multiboot_mmap_entry e[1]; } M;
        struct aspace A, B, Z;
        u32 free0, fa, fb;
        u32 f = 0;

        memset(fake_ram, 0xAA, sizeof fake_ram);                              /* cadres "sales" : on doit les retrouver a zero */
        M.mbi.flags = MB_FLAG_MMAP;
        M.mbi.mmap_addr = (u32)M.e;
        M.mbi.mmap_length = sizeof M.e;
        M.e[0] = (struct multiboot_mmap_entry){ 20, (u32)fake_ram, 0, sizeof fake_ram, 0, MB_MMAP_AVAILABLE };
        pmm_init(&M.mbi);
        free0 = pmm_free_frames();

        CHECK(paging_build(dir, tabs, 32, &c) == E_OK);
        CHECK(aspace_create(&Z) == E_INVAL && Z.dir == NULL && pmm_free_frames() == free0);   /* repertoire du noyau inconnu */
        aspace_set_kernel_dir(dir);

        CHECK(aspace_create(&A) == E_OK && A.pages == 0 && pmm_free_frames() == free0 - 2);
        CHECK(((u32)A.dir & 0xFFF) == 0 && ((u32)A.pt & 0xFFF) == 0);
        CHECK(A.dir[0] == dir[0] && A.dir[2] == dir[2] && A.dir[31] == dir[31] && A.dir[32] == dir[32]);   /* noyau repris */
        CHECK(A.dir[1] != dir[1] && A.dir[1] == (((u32)A.pt) | PAGE_P | PAGE_RW | PAGE_US));
        CHECK(paging_lookup(A.dir, 0x400000, &pa, NULL) == E_FAULT);        /* rien de mappe */
        CHECK(paging_lookup(A.dir, 0x123456, &pa, NULL) == E_OK && pa == 0x123456);   /* le noyau est la */

        /* mapper */
        CHECK(aspace_map(&A, 0x400000, 0x402001, 1) == E_OK && A.pages == 3);   /* 0x402001 : 3 pages */
        CHECK(paging_lookup(A.dir, 0x400000, &pa, &f) == E_OK && f == (PAGE_P | PAGE_RW | PAGE_US));
        CHECK(paging_lookup(A.dir, 0x402FFF, &pa, NULL) == E_OK && paging_lookup(A.dir, 0x403000, &pa, NULL) == E_FAULT);
        CHECK(((u8 *)(A.pt[0] & ~0xFFFu))[0] == 0 && ((u8 *)(A.pt[2] & ~0xFFFu))[4095] == 0);   /* mis a zero */
        fa = A.pt[0] & ~0xFFFu;
        CHECK(aspace_map(&A, 0x400000, 0x401000, 1) == E_OK && A.pages == 3 && (A.pt[0] & ~0xFFFu) == fa);   /* deja mappe */
        CHECK(aspace_map(&A, 0x7F0000, 0x800000, 1) == E_OK && A.pages == 19);  /* la pile, en haut */
        CHECK(paging_lookup(A.dir, 0x7FFFFF, &pa, NULL) == E_OK);
        CHECK(aspace_map(&A, 0x3FF000, 0x400000, 1) == E_INVAL);               /* hors zone */
        CHECK(aspace_map(&A, 0x400000, 0x800001, 1) == E_INVAL);
        CHECK(aspace_map(&A, 0x500000, 0x500000, 1) == E_INVAL);
        CHECK(aspace_map(&A, 0x600000, 0x500000, 1) == E_INVAL);
        CHECK(aspace_map(&A, 0x800000, 0x801000, 1) == E_INVAL);
        CHECK(A.pages == 19);

        /* le noyau reste hors de portee du ring 3, meme depuis l'espace du programme */
        CHECK(paging_lookup(A.dir, 0x100000, NULL, &f) == E_OK && !(f & PAGE_US) && !(f & PAGE_RW));
        CHECK(paging_lookup(A.dir, 0x200000, NULL, &f) == E_OK && !(f & PAGE_US));

        /* aspace_put : ecriture directe dans les cadres, a cheval sur plusieurs pages */
        {
            u8 src[6000], got[6000];

            for (int i = 0; i < 6000; i++) src[i] = (u8)(i * 7 + 1);
            CHECK(aspace_put(&A, 0x400F00, src, 6000) == E_OK);
            for (u32 i = 0; i < 6000; i++) {
                u32 va = 0x400F00 + i;
                got[i] = ((u8 *)(A.pt[(va - 0x400000) >> 12] & ~0xFFFu))[va & 0xFFF];
            }
            CHECK(memcmp(src, got, 6000) == 0);
            CHECK(aspace_put(&A, 0x401000, NULL, 100) == E_OK && ((u8 *)(A.pt[1] & ~0xFFFu))[50] == 0 && ((u8 *)(A.pt[1] & ~0xFFFu))[100] == src[0x164]);
            CHECK(aspace_put(&A, 0x400000, src, 0) == E_OK);
            CHECK(aspace_put(&A, 0x3FFFFF, src, 2) == E_FAULT && aspace_put(&A, 0x7FFFFF, src, 2) == E_FAULT);
            CHECK(aspace_put(&A, 0x700000, src, 4) == E_FAULT);                 /* page non mappee */
            CHECK(aspace_put(&A, 0xFFFFFFF0u, src, 0x20) == E_FAULT);
        }

        /* verification des pointeurs que donne un programme */
        CHECK(aspace_check(&A, 0x400000, 0x3000, 1) && aspace_check(&A, 0x402FFF, 1, 0));
        CHECK(!aspace_check(&A, 0x400000, 0x3001, 0));                          /* depasse sur une page absente */
        CHECK(!aspace_check(&A, 0x403000, 1, 0) && !aspace_check(&A, 0x500000, 4, 0));
        CHECK(!aspace_check(&A, 0x100000, 4, 0) && !aspace_check(&A, 0, 4, 0));   /* noyau, adresse 0 */
        CHECK(!aspace_check(&A, 0x3FFFFF, 2, 0) && !aspace_check(&A, 0x7FFFFF, 2, 0));   /* a cheval sur une frontiere de zone */
        CHECK(!aspace_check(&A, 0xFFFFFFFFu, 2, 0) && !aspace_check(&A, 0x7FFFF0, 0xFFFFFFF0u, 0));   /* debordement */
        CHECK(aspace_check(&A, 0x7FFFFF, 1, 1) && aspace_check(&A, 0x400000, 0, 0));
        CHECK(aspace_protect(&A, 0x400000, 0x401000, 0) == E_OK);
        CHECK(aspace_check(&A, 0x400000, 16, 0) && !aspace_check(&A, 0x400000, 16, 1));   /* lecture seule */
        CHECK(aspace_check(&A, 0x401000, 16, 1));                               /* page voisine inchangee */
        CHECK(!aspace_check(&A, 0x400FF0, 0x20, 1) && aspace_check(&A, 0x400FF0, 0x20, 0));   /* ecriture a cheval */
        CHECK(paging_lookup(A.dir, 0x400000, NULL, &f) == E_OK && f == (PAGE_P | PAGE_US));
        CHECK(aspace_protect(&A, 0x400000, 0x401000, 1) == E_OK && aspace_check(&A, 0x400000, 16, 1));
        CHECK(aspace_protect(&A, 0x300000, 0x400000, 0) == E_INVAL);
        CHECK(aspace_protect(&A, 0x600000, 0x601000, 0) == E_OK && !aspace_check(&A, 0x600000, 1, 0));   /* page absente : ignoree */

        /* isolation : meme adresse virtuelle, memoire differente */
        CHECK(aspace_create(&B) == E_OK && B.dir != A.dir && B.pt != A.pt);
        CHECK(aspace_map(&B, 0x400000, 0x401000, 1) == E_OK && B.pages == 1);
        fb = B.pt[0] & ~0xFFFu;
        CHECK(fb != fa);
        *(volatile u32 *)fa = 0xDEADBEEF;
        CHECK(*(volatile u32 *)fb == 0);                                         /* B ne voit pas ce qu'ecrit A */
        CHECK(paging_lookup(A.dir, 0x400000, &pa, NULL) == E_OK && pa == fa);
        CHECK(paging_lookup(B.dir, 0x400000, &pa, NULL) == E_OK && pa == fb);
        CHECK(paging_lookup(B.dir, 0x401000, &pa, NULL) == E_FAULT);            /* B n'a pas les pages de A */
        CHECK(!aspace_check(&B, 0x7FFFF0, 4, 0) && aspace_check(&A, 0x7FFFF0, 4, 0));

        /* detruire rend tout, sans toucher a l'autre espace */
        aspace_destroy(&A);
        CHECK(A.dir == NULL && A.pt == NULL && A.pages == 0);
        aspace_destroy(&A);                                                      /* deux fois : sans danger */
        CHECK(pmm_free_frames() == free0 - 3 && aspace_check(&B, 0x400000, 4, 1));
        aspace_destroy(&B);
        CHECK(pmm_free_frames() == free0);
        CHECK(aspace_cr3(&B) == 0 && !aspace_check(&B, 0x400000, 4, 0) && aspace_map(&B, 0x400000, 0x401000, 1) == E_INVAL);

        /* plus de memoire : tout ce qu'un appel a mappe est rendu */
        CHECK(aspace_create(&A) == E_OK);
        CHECK(aspace_map(&A, 0x400000, 0x403000, 1) == E_OK);
        {
            u32 keep = 8, avail = pmm_free_frames(), n = 0, *grab = (u32 *)tabs;   /* tabs : tableau libre pour noter les cadres pris */

            while (pmm_free_frames() > keep && n < 4096)
                grab[n++] = pmm_alloc_frame();
            CHECK(pmm_free_frames() == keep && avail > keep);
            CHECK(aspace_map(&A, 0x500000, 0x510000, 1) == E_NOMEM);            /* 16 pages demandees, 8 disponibles */
            CHECK(pmm_free_frames() == keep && A.pages == 3 && !aspace_check(&A, 0x500000, 1, 0));
            CHECK(aspace_check(&A, 0x400000, 0x3000, 1));                        /* l'existant est intact */
            CHECK(aspace_map(&A, 0x500000, 0x504000, 1) == E_OK && pmm_free_frames() == keep - 4);   /* 4 pages tiennent */
            CHECK(aspace_create(&B) == E_OK && pmm_free_frames() == keep - 6);
            aspace_destroy(&B);
            while (pmm_free_frames() > 1)
                (void)pmm_alloc_frame();
            CHECK(aspace_create(&B) == E_NOMEM && B.dir == NULL && pmm_free_frames() == 1);   /* repertoire sans table : rien de garde */
            aspace_destroy(&A);
            for (u32 i = 0; i < n; i++)
                pmm_free_frame(grab[i]);
        }
        {
            u32 left = pmm_free_frames();

            for (int i = 0; i < 1000; i++) {                                     /* pas de fuite apres mille programmes */
                CHECK(aspace_create(&A) == E_OK && aspace_map(&A, 0x400000, 0x420000, 1) == E_OK);
                aspace_destroy(&A);
            }
            CHECK(pmm_free_frames() == left);
        }
        CHECK(act_user == 0 && act_kernel == 0);                                 /* le test ne touche jamais CR3 */
    }


    out("  espaces d'adressage verifies : creation, mappage, droits, isolation entre programmes, memoire pleine, 1000 cycles sans fuite\n");
    return 0;
}
