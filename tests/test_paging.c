/* Test du vrai paging.c (tables de pages) et de pmm_top / pmm_limit. */
#include "testlib.h"
#include "paging.h"
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

static struct paging_cfg kcfg(void)
{
    struct paging_cfg c = { TOP, 0x100000, 0x115000, 0x400000, 0x800000, 0xA0000, 0xC0000 };
    return c;
}

static u32 flags_of(u32 va)
{
    u32 f = 0xFFFF;

    return paging_lookup(dir, va, NULL, &f) == E_OK ? f : 0xFFFF;
}

int test_main(void)
{
    struct paging_cfg c = kcfg();
    u32 pa = 0;

    CHECK(paging_tables_for(4u << 20) == 1 && paging_tables_for((4u << 20) + 1) == 2);
    CHECK(paging_tables_for(TOP) == 32 && paging_tables_for(PAGE_SIZE) == 1 && paging_tables_for(PAGING_MAX_TOP) == 256);

    CHECK(paging_build(dir, tabs, 32, &c) == E_OK);

    /* page nulle absente ; noyau et donnees en RW sans acces ring 3 */
    CHECK(paging_lookup(dir, 0, &pa, NULL) == E_FAULT);
    CHECK(paging_lookup(dir, 0xFFF, &pa, NULL) == E_FAULT);
    CHECK(flags_of(0x1000) == (PAGE_P | PAGE_RW));
    CHECK(flags_of(0x9F000) == (PAGE_P | PAGE_RW));
    CHECK(flags_of(0xB8000) == (PAGE_P | PAGE_RW));
    CHECK((tabs[0xB8] & (PAGE_PCD | PAGE_PWT)) == (PAGE_PCD | PAGE_PWT));       /* memoire video sans cache */
    CHECK((tabs[0x9F] & PAGE_PCD) == 0 && (tabs[0xC0] & PAGE_PCD) == 0 && (tabs[0xA0] & PAGE_PCD) != 0);
    /* code et constantes du noyau : lecture seule, jamais ring 3 */
    CHECK(flags_of(0x100000) == PAGE_P);
    CHECK(flags_of(0x114FFF) == PAGE_P);
    CHECK(flags_of(0x115000) == (PAGE_P | PAGE_RW));                           /* .data : ecriture */
    /* zone des programmes : seule accessible au ring 3 */
    CHECK(flags_of(0x3FF000) == (PAGE_P | PAGE_RW));
    CHECK(flags_of(0x400000) == (PAGE_P | PAGE_RW | PAGE_US));
    CHECK(flags_of(0x7FFFFF) == (PAGE_P | PAGE_RW | PAGE_US));
    CHECK(flags_of(0x800000) == (PAGE_P | PAGE_RW));
    /* identite, et bornes de la RAM */
    CHECK(paging_lookup(dir, 0x123456, &pa, NULL) == E_OK && pa == 0x123456);
    CHECK(paging_lookup(dir, TOP - 1, &pa, NULL) == E_OK && pa == TOP - 1);
    CHECK(paging_lookup(dir, TOP, &pa, NULL) == E_FAULT);
    CHECK(paging_lookup(dir, 0xFFFFF000u, &pa, NULL) == E_FAULT);

    /* parcours complet : chaque page correspond exactement aux regles */
    {
        int bad = 0;

        for (u32 a = 0; a < TOP; a += PAGE_SIZE) {
            u32 want, f = 0;
            err_t e = paging_lookup(dir, a, &pa, &f);

            if (a == 0) { if (e != E_FAULT) bad++; continue; }
            want = PAGE_P | PAGE_RW;
            if (a >= 0x100000 && a < 0x115000) want &= ~PAGE_RW;
            if (a >= 0x400000 && a < 0x800000) want |= PAGE_US;
            if (e != E_OK || pa != a || f != want) bad++;
        }
        CHECK(bad == 0);
    }

    /* sans zone utilisateur ni lecture seule */
    c = kcfg(); c.user_lo = c.user_hi = c.ro_lo = c.ro_hi = 0;
    CHECK(paging_build(dir, tabs, 32, &c) == E_OK);
    CHECK(flags_of(0x400000) == (PAGE_P | PAGE_RW) && flags_of(0x100000) == (PAGE_P | PAGE_RW));

    /* RAM qui n'est pas un multiple de 4 Mio : la derniere table est partielle */
    c = kcfg(); c.map_top = (6u << 20) + 3 * PAGE_SIZE; c.user_lo = c.user_hi = 0;
    CHECK(paging_build(dir, tabs, 2, &c) == E_OK);
    CHECK(flags_of(c.map_top - PAGE_SIZE) == (PAGE_P | PAGE_RW));
    CHECK(flags_of(c.map_top) == 0xFFFF && flags_of(c.map_top + PAGE_SIZE) == 0xFFFF && flags_of(7u << 20) == 0xFFFF);
    CHECK(paging_lookup(dir, 8u << 20, &pa, NULL) == E_FAULT);                  /* table suivante absente */

    /* configurations invalides : refusees */
    c = kcfg(); c.map_top = 0;                    CHECK(paging_build(dir, tabs, 32, &c) == E_INVAL);
    c = kcfg(); c.map_top = TOP + 1;              CHECK(paging_build(dir, tabs, 32, &c) == E_INVAL);
    c = kcfg(); c.map_top = PAGING_MAX_TOP + PAGE_SIZE; CHECK(paging_build(dir, tabs, 32, &c) == E_INVAL);
    c = kcfg();                                   CHECK(paging_build(dir, tabs, 31, &c) == E_INVAL);   /* mauvais nombre de tables */
    c = kcfg();                                   CHECK(paging_build(dir, tabs, 33, &c) == E_INVAL);
    c = kcfg(); c.user_lo = 0x100000;             CHECK(paging_build(dir, tabs, 32, &c) == E_INVAL);   /* ring 3 dans le noyau */
    c = kcfg(); c.user_lo = 0x114000; c.user_hi = 0x500000; CHECK(paging_build(dir, tabs, 32, &c) == E_INVAL);
    c = kcfg(); c.user_hi = TOP + PAGE_SIZE;      CHECK(paging_build(dir, tabs, 32, &c) == E_INVAL);   /* hors RAM */
    c = kcfg(); c.user_lo = 0x800000; c.user_hi = 0x400000; CHECK(paging_build(dir, tabs, 32, &c) == E_INVAL);
    c = kcfg(); c.ro_lo = 0x100001;               CHECK(paging_build(dir, tabs, 32, &c) == E_INVAL);   /* non aligne */
    c = kcfg();                                   CHECK(paging_build(NULL, tabs, 32, &c) == E_INVAL);
    c = kcfg();                                   CHECK(paging_build(dir, NULL, 32, &c) == E_INVAL);
    c = kcfg();                                   CHECK(paging_build((u32 *)((u8 *)dir + 4), tabs, 32, &c) == E_INVAL);
    c = kcfg();                                   CHECK(paging_build(dir, (u32 *)((u8 *)tabs + 8), 32, &c) == E_INVAL);

    /* pmm_top / pmm_limit */
    {
        static struct { struct multiboot_info mbi; struct multiboot_mmap_entry e[1]; } M;
        u32 free0, got, seen_high = 0;

        M.mbi.flags = MB_FLAG_MMAP;
        M.mbi.mmap_addr = (u32)M.e;
        M.mbi.mmap_length = sizeof M.e;
        M.e[0] = (struct multiboot_mmap_entry){ 20, (u32)fake_ram, 0, sizeof fake_ram, 0, MB_MMAP_AVAILABLE };
        pmm_init(&M.mbi);
        CHECK(pmm_top() == (((u32)fake_ram + sizeof fake_ram) & ~0xFFFu));
        free0 = pmm_free_frames();
        pmm_limit((u32)fake_ram + (4u << 20));                                /* garde les 4 premiers Mio */
        CHECK(pmm_free_frames() <= free0 - 1000 && pmm_free_frames() >= 1000);
        pmm_limit((u32)fake_ram + (4u << 20));                                /* idempotent */
        got = pmm_free_frames();
        for (u32 i = 0; i < got + 5; i++) {
            u32 f = pmm_alloc_frame();

            if (!f) break;
            if (f >= (u32)fake_ram + (4u << 20)) seen_high = 1;
        }
        CHECK(!seen_high && pmm_free_frames() == 0);
        pmm_limit(0xFFFFF000u);                                               /* au-dela de la RAM : sans effet */
    }

    out("  pagination verifiee : droits page par page (128 Mio), page nulle, noyau en lecture seule, ring 3, configurations invalides\n");
    return 0;
}
