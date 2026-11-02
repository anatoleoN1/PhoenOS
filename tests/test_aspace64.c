/* aspace.c en version 64 bits (perf64) : PML4/PDPT/PD/PT propres a chaque programme, avec un parcours de tables logiciel. */
#include "testlib.h"
#include "aspace.h"
#include "paging.h"
#include "pmm.h"
#include "multiboot.h"

u8 test_scancode;
char kernel_start[4096] __attribute__((aligned(4096)));
char kernel_end[1];
void test_panic(const char *fmt, ...) { out("  PANIQUE INATTENDUE : "); out(fmt); out("\n"); exit_now(2); }
static char fake_ram[4u << 20] __attribute__((aligned(4096)));

static u64 k4[512] __attribute__((aligned(4096)));
static u64 k3[512] __attribute__((aligned(4096)));
static u64 kpd[4][512] __attribute__((aligned(4096)));

/* Parcours des 4 niveaux : adresse physique et droits effectifs (P, RW, US = ET des niveaux). 0 : non present. */
static int walk(const struct aspace *a, u64 va, u64 *pa, u64 *fl)
{
    u64 e = a->dir[(va >> 39) & 511], rw = e & PAGE_RW, us = e & PAGE_US;
    const u64 *t;

    if (!(e & PAGE_P)) return 0;
    t = (const u64 *)(usize)(e & 0xFFFFF000ull); e = t[(va >> 30) & 511]; rw &= e; us &= e;
    if (!(e & PAGE_P)) return 0;
    t = (const u64 *)(usize)(e & 0xFFFFF000ull); e = t[(va >> 21) & 511]; rw &= e; us &= e;
    if (!(e & PAGE_P)) return 0;
    if (e & 0x80) {                                     /* page de 2 Mio */
        *pa = (e & 0xFFE00000ull) | (va & 0x1FFFFF); *fl = PAGE_P | rw | us; return 1;
    }
    t = (const u64 *)(usize)(e & 0xFFFFF000ull); e = t[(va >> 12) & 511]; rw &= e; us &= e;
    if (!(e & PAGE_P)) return 0;
    *pa = (e & 0xFFFFF000ull) | (va & 0xFFF); *fl = PAGE_P | rw | us;
    return 1;
}

int test_main(void)
{
    static struct { struct multiboot_info mbi; struct multiboot_mmap_entry e[1]; } M;
    struct aspace A, B, Z;
    u32 free0;
    u64 pa, fl;

    memset(fake_ram, 0xAA, sizeof fake_ram);
    M.mbi.flags = MB_FLAG_MMAP;
    M.mbi.mmap_addr = (u32)(usize)M.e;
    M.mbi.mmap_length = sizeof M.e;
    M.e[0] = (struct multiboot_mmap_entry){ 20, (u32)(usize)fake_ram, 0, sizeof fake_ram, 0, MB_MMAP_AVAILABLE };
    pmm_init(&M.mbi);
    free0 = pmm_free_frames();

    /* "noyau" : identite sur 4 Gio en pages de 2 Mio, comme boot.S */
    k4[0] = (u64)(usize)k3 | 3;
    for (int i = 0; i < 4; i++) {
        k3[i] = (u64)(usize)kpd[i] | 3;
        for (int j = 0; j < 512; j++)
            kpd[i][j] = ((u64)(i * 512 + j) << 21) | 0x83;
    }

    CHECK(aspace_create(&Z) == E_INVAL && Z.dir == NULL && pmm_free_frames() == free0);
    aspace_set_kernel_dir(k4);

    CHECK(aspace_create(&A) == E_OK && A.pages == 0 && pmm_free_frames() == free0 - 5);   /* PML4 + PDPT + PD + 2 tables */
    CHECK(((usize)A.dir & 0xFFF) == 0 && ((usize)A.pt & 0xFFF) == 0 && A.pt != NULL);
    CHECK(A.dir != k4 && A.dir[0] != k4[0] && ((u64 *)(usize)A.pdpt)[1] == k3[1] && ((u64 *)(usize)A.pdpt)[3] == k3[3]);
    CHECK(((u64 *)(usize)A.pd)[0] == kpd[0][0] && ((u64 *)(usize)A.pd)[4] == kpd[0][4]);   /* le reste du noyau est repris */
    CHECK(kpd[0][2] == (((u64)2 << 21) | 0x83));                                          /* modele du noyau intact */

    /* le noyau est visible mais interdit au ring 3 ; la zone des programmes est vide */
    CHECK(walk(&A, 0x100000, &pa, &fl) && pa == 0x100000 && !(fl & PAGE_US));
    CHECK(!walk(&A, 0x400000, &pa, &fl) || 0);

    CHECK(aspace_map(&A, 0x400000, 0x402001, 1) == E_OK && A.pages == 3);
    CHECK(walk(&A, 0x400000, &pa, &fl) && fl == (PAGE_P | PAGE_RW | PAGE_US) && pa >= (u64)(usize)fake_ram);
    CHECK(walk(&A, 0x402FFF, &pa, &fl) && !walk(&A, 0x403000, &pa, &fl));
    CHECK(aspace_map(&A, 0x7F0000, 0x800000, 1) == E_OK && A.pages == 19);                /* 2e table (6-8 Mio) */
    CHECK(walk(&A, 0x7FFFFF, &pa, &fl) && (fl & PAGE_US));
    CHECK(aspace_map(&A, 0x3FF000, 0x400000, 1) == E_INVAL && aspace_map(&A, 0x400000, 0x800001, 1) == E_INVAL);

    /* aspace_put a cheval sur des pages, relu par le parcours des tables */
    {
        u8 src[6000];

        for (int i = 0; i < 6000; i++) src[i] = (u8)(i * 7 + 1);
        CHECK(aspace_put(&A, 0x400F00, src, 6000) == E_OK);
        for (u32 i = 0; i < 6000; i += 97) {
            u64 p;

            CHECK(walk(&A, 0x400F00 + i, &p, &fl) && *(u8 *)(usize)p == src[i]);
        }
        CHECK(aspace_put(&A, 0x3FFFF0, src, 32) == E_FAULT && aspace_put(&A, 0x7FFFF0, src, 16) == E_OK && aspace_put(&A, 0x7FFFF0, src, 17) == E_FAULT);
    }

    CHECK(aspace_check(&A, 0x400000, 0x3000, 1) && !aspace_check(&A, 0x400000, 0x3001, 0) && !aspace_check(&A, 0x100000, 16, 0));
    CHECK(aspace_protect(&A, 0x400000, 0x401000, 0) == E_OK && !aspace_check(&A, 0x400000, 16, 1) && aspace_check(&A, 0x400000, 16, 0));
    CHECK(walk(&A, 0x400000, &pa, &fl) && !(fl & PAGE_RW));

    /* isolation : un second programme ne voit pas les pages du premier */
    CHECK(aspace_create(&B) == E_OK && !walk(&B, 0x400000, &pa, &fl));
    CHECK(aspace_map(&B, 0x400000, 0x401000, 1) == E_OK);
    {
        u64 pa_a, pa_b;

        CHECK(walk(&A, 0x400000, &pa_a, &fl) && walk(&B, 0x400000, &pa_b, &fl) && pa_a != pa_b);
    }

    /* page partagee en lecture seule, jamais rendue */
    {
        u32 shared = pmm_alloc_frame(), fr = pmm_free_frames();

        CHECK(aspace_map_shared(&B, 0x500000, shared) == E_OK && walk(&B, 0x500000, &pa, &fl) && pa == shared && !(fl & PAGE_RW));
        CHECK(aspace_put(&B, 0x500000, (const u8 *)"x", 1) == E_FAULT);
        aspace_destroy(&B);
        CHECK(pmm_free_frames() == fr + 5 + 1);                                           /* 5 tables + 1 page privee, pas la partagee */
        pmm_free_frame(shared);
    }

    CHECK(aspace_unmap(&A, 0x400000, 0x401000) == E_OK && A.pages == 18 && !walk(&A, 0x400000, &pa, &fl));
    aspace_destroy(&A);
    CHECK(A.dir == NULL && pmm_free_frames() == free0);                                   /* aucune fuite */

    /* memoire pleine : tout est rendu */
    {
        struct aspace C;
        u32 hold[2048], n = 0, f;

        while (n < 2048 && (f = pmm_alloc_frame()))
            hold[n++] = f;
        for (int i = 0; i < 4; i++) pmm_free_frame(hold[--n]);                            /* 4 cadres libres : insuffisant (5 + contigus) */
        CHECK(aspace_create(&C) == E_NOMEM && C.dir == NULL && pmm_free_frames() == 4);
        while (n) pmm_free_frame(hold[--n]);
        CHECK(pmm_free_frames() == free0);
    }
    return 0;
}
