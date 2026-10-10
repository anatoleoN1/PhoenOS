/*
 * Tests de pmm.c et kheap.c (vrai code du noyau) sur une fausse RAM de 40 Mio.
 * MODE 0 : gestionnaire de frames ; 1 : tas ; 2..6 : erreurs qui doivent déclencher PANIC.
 */
#include <stdarg.h>
#include "testlib.h"
#include "multiboot.h"
#include "pmm.h"
#include "kheap.h"
#include "panic.h"

#ifndef MODE
#define MODE 0
#endif

char kernel_start[4096] __attribute__((aligned(4096)));
char kernel_end[1];
static char fake_ram[40u << 20] __attribute__((aligned(4096)));
u8 test_scancode;

static const char *expect;
void test_panic(const char *fmt, ...)
{
    if (expect) {
        usize n = strlen(expect), m = strlen(fmt);

        for (usize i = 0; i + n <= m; i++) {
            if (!memcmp(fmt + i, expect, n)) {
                out("  panique attendue : "); out(fmt); out("\n");
                exit_now(EXIT_EXPECTED_PANIC);
            }
        }
    }
    out("  PANIQUE INATTENDUE : "); out(fmt); out("\n");
    exit_now(2);
}

static struct { struct multiboot_info mbi; struct multiboot_mmap_entry e[3]; } M;

static void setup(void)
{
    memset(&M, 0, sizeof M);
    M.mbi.flags = MB_FLAG_MMAP;
    M.mbi.mmap_addr = (u32)M.e;
    M.mbi.mmap_length = sizeof M.e;
    M.e[0] = (struct multiboot_mmap_entry){ 20, 0, 0, 0xA0000, 0, MB_MMAP_AVAILABLE };
    M.e[1] = (struct multiboot_mmap_entry){ 20, 0xA0000, 0, 0x60000, 0, 2 };
    M.e[2] = (struct multiboot_mmap_entry){ 20, (u32)fake_ram, 0, sizeof fake_ram, 0, MB_MMAP_AVAILABLE };
    pmm_init(&M.mbi);
}

static void test_pmm(void)
{
    u32 usable, free0, a, b, c, d, n;

    setup();
    usable = pmm_usable_frames();
    free0  = pmm_free_frames();
    CHECK(usable >= 10000 && free0 <= usable);

    a = pmm_alloc_frame(); b = pmm_alloc_frame();
    CHECK(a && b && a != b && !(a & 0xFFF));
    CHECK(a >= (u32)fake_ram && a < (u32)fake_ram + sizeof fake_ram);
    pmm_free_frame(a); pmm_free_frame(b);
    CHECK(pmm_free_frames() == free0);

    c = pmm_alloc_contiguous(100);
    CHECK(c && !(c & 0xFFF) && pmm_free_frames() == free0 - 100);
    d = pmm_alloc_frame();
    CHECK(d && (d < c || d >= c + 100 * 4096));
    pmm_free_frame(d);
    for (u32 i = 0; i < 100; i++)
        pmm_free_frame(c + i * 4096);
    CHECK(pmm_free_frames() == free0);

    n = 0;
    while (pmm_alloc_frame())
        n++;
    CHECK(n == free0 && pmm_free_frames() == 0 && pmm_alloc_contiguous(2) == 0);

    out("  pmm : "); outnum(usable); out(" frames detectees, "); outnum(n); out(" allouees puis epuisees\n");
}

static void test_heap(void)
{
    struct kheap_stats s0, s1, s2;
    static void *big[20000];
    u8 *p[8], *z;
    u32 n;

    setup();
    kheap_init();
    kheap_get_stats(&s0);
    CHECK(s0.free_blocks == 1 && s0.used_blocks == 0);

    for (int i = 0; i < 8; i++) {
        p[i] = kmalloc(24u << i);
        CHECK(p[i] && !((u32)p[i] & 15));
        memset(p[i], 0xA0 + i, 24u << i);
    }
    for (int i = 0; i < 8; i++)
        for (u32 j = 0; j < (24u << i); j++)
            if (p[i][j] != 0xA0 + i) { test_fails++; break; }
    kheap_check();

    for (int i = 0; i < 8; i += 2) kfree(p[i]);
    kheap_get_stats(&s1);
    CHECK(s1.used_blocks == 4);
    for (int i = 1; i < 8; i += 2) kfree(p[i]);
    kheap_check();
    kheap_get_stats(&s2);
    CHECK(s2.used_blocks == 0 && s2.free_blocks == 1 && s2.free_bytes == s0.free_bytes);

    z = kzalloc(500); memset(z, 0xFF, 500); kfree(z);
    z = kzalloc(500);
    for (int j = 0; j < 500; j++) if (z[j]) { test_fails++; break; }
    kfree(z);

    CHECK(kmalloc(0) == NULL && kmalloc(0x7FFFFFFFu) == NULL);
    kfree(NULL);

    n = 0;
    while (n < 20000 && (big[n] = kmalloc(1000)))
        n++;
    CHECK(n > 1000 && n < 20000);
    kheap_check();
    for (u32 i = n; i--; )
        kfree(big[i]);
    kheap_get_stats(&s2);
    CHECK(s2.used_blocks == 0 && s2.free_blocks == 1 && s2.free_bytes == s0.free_bytes);

    out("  tas : "); outnum(kheap_size() / 1024); out(" Kio, ");
    outnum(n); out(" blocs de 1000 octets alloues puis tout refusionne\n");
}

/* Les modules GRUB (initrd) ne doivent jamais être donnés par pmm_alloc_frame. */
static void test_modules(void)
{
    u32 base = (u32)fake_ram;
    struct multiboot_module *mod = (struct multiboot_module *)(base + (5u << 20));
    char *name = (char *)(base + (6u << 20));
    u32 m0 = base + (1u << 20), m1 = m0 + 300 * 1024, f, got = 0;

    setup();
    memcpy(name, "/boot/initrd.tar", 17);
    mod[0] = (struct multiboot_module){ m0, m1, (u32)name, 0 };
    M.mbi.flags |= MB_FLAG_MODS;
    M.mbi.mods_count = 1;
    M.mbi.mods_addr = (u32)mod;
    pmm_init(&M.mbi);
    while ((f = pmm_alloc_frame()) != 0) {
        got++;
        CHECK(!(f >= (m0 & ~0xFFFu) && f < m1));                /* corps du module */
        CHECK(!(f == (u32)mod >> 12 << 12));                    /* tableau des modules */
        CHECK(!(f == ((u32)name >> 12 << 12)));                 /* nom du module */
        if (test_fails > 3)
            break;
    }
    CHECK(got > 5000);
    out("  modules GRUB proteges : "); outnum(got); out(" frames allouees, aucune dans l'initrd\n");
}

int test_main(void)
{
#if MODE == 7
    test_modules();
#elif MODE == 0
    test_pmm();
#elif MODE == 1
    test_heap();
#else
    setup();
    kheap_init();
#if MODE == 2
    expect = "Double liberation detectee";
    { void *p = kmalloc(64); kfree(p); kfree(p); }
#elif MODE == 3
    expect = "Corruption du tas";
    { u8 *p = kmalloc(32); u8 *q = kmalloc(32); memset(p, 0x41, 32 + 16 + 4); kfree(q); }
#elif MODE == 4
    expect = "hors du tas";
    kfree((void *)0x1234);
#elif MODE == 5
    expect = "Double liberation de la frame";
    { u32 f = pmm_alloc_frame(); pmm_free_frame(f); pmm_free_frame(f); }
#elif MODE == 6
    expect = "frame reservee";
    pmm_free_frame(0x2000);
#endif
    out("  (aucune panique : echec du test)\n");
    return 1;
#endif
    return 0;
}
