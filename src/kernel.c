/*
 * PhoenOS - kernel.c
 * Point d'entrée du noyau (appelé depuis boot.S).
 */
#include "types.h"
#include "multiboot.h"
#include "vga.h"
#include "serial.h"
#include "kprintf.h"
#include "log.h"
#include "panic.h"
#include "io.h"
#include "gdt.h"
#include "isr.h"
#include "pic.h"
#include "pit.h"
#include "keyboard.h"
#include "pmm.h"
#include "kheap.h"
#include "string.h"
#include "version.h"
#include "shell.h"
#include "fs.h"
#include "initrd.h"

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

static void banner(void)
{
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    kprintf("  ____  _                      ___  ____\n");
    kprintf(" |  _ \\| |__   ___   ___ _ __ / _ \\/ ___|\n");
    kprintf(" | |_) | '_ \\ / _ \\ / _ \\ '_ \\ | | \\___ \\\n");
    kprintf(" |  __/| | | | (_) |  __/ | | | |_| |___) |\n");
    kprintf(" |_|   |_| |_|\\___/ \\___|_| |_|\\___/|____/\n\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

static void status(int good, const char *msg)
{
    vga_set_color(good ? VGA_LIGHT_GREEN : VGA_LIGHT_RED, VGA_BLACK);
    kprintf(good ? "[ OK ] " : "[FAIL] ");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf("%s\n", msg);
}

/* Auto-test du gestionnaire de frames : allocation, libération, réutilisation. */
static int selftest_pmm(void)
{
    u32 before = pmm_free_frames();
    u32 a = pmm_alloc_frame();
    u32 b = pmm_alloc_frame();
    int ok = a && b && a != b && !(a & 0xFFF) && !(b & 0xFFF);

    ok = ok && pmm_free_frames() == before - 2;
    if (a) pmm_free_frame(a);
    if (b) pmm_free_frame(b);
    return ok && pmm_free_frames() == before;
}

/* Auto-test du tas : blocs de tailles variées, motif d'écriture, fusion des blocs libres. */
static int selftest_heap(void)
{
    struct kheap_stats s0, s1;
    u8 *p[8];
    int ok = 1;

    kheap_get_stats(&s0);

    for (int i = 0; i < 8; i++) {
        p[i] = kmalloc(24u << i);               /* 24, 48, ... 3072 octets */
        if (!p[i])
            return 0;
        memset(p[i], 0xA0 + i, 24u << i);
    }
    for (int i = 0; i < 8; i++)                 /* aucun bloc n'a écrasé un autre */
        for (u32 j = 0; j < (24u << i); j++)
            if (p[i][j] != 0xA0 + i)
                ok = 0;
    kheap_check();

    for (int i = 0; i < 8; i += 2)              /* libère un bloc sur deux, puis le reste */
        kfree(p[i]);
    for (int i = 1; i < 8; i += 2)
        kfree(p[i]);
    kheap_check();

    kheap_get_stats(&s1);                       /* tout doit avoir fusionné en un seul bloc */
    ok = ok && s1.used_blocks == 0 && s1.free_blocks == s0.free_blocks &&
         s1.free_bytes == s0.free_bytes;

    /* kzalloc doit rendre de la mémoire à zéro, même sur un bloc réutilisé */
    u8 *z = kzalloc(256);
    if (!z)
        return 0;
    for (u32 j = 0; j < 256; j++)
        if (z[j])
            ok = 0;
    kfree(z);

    return ok && kmalloc(0) == NULL && kmalloc(0x7FFFFFFFu) == NULL;
}

/*
 * Tests de panique volontaires :
 *   make test-div0 | test-gp | test-panic | test-assert | test-doublefree | test-heapcorrupt
 * (sans option, cette fonction ne fait rien).
 */
static void run_selected_test(void)
{
#if defined(TEST_DIV0)
    kprintf("\nTEST : division par zero dans 2 secondes...\n");
    pit_sleep_ms(2000);
    __asm__ volatile (
        "xorl %%edx, %%edx\n\t"
        "movl $1, %%eax\n\t"
        "xorl %%ecx, %%ecx\n\t"
        "divl %%ecx"
        : : : "eax", "ecx", "edx");
#elif defined(TEST_GP)
    kprintf("\nTEST : chargement d'un selecteur invalide dans 2 secondes...\n");
    pit_sleep_ms(2000);
    __asm__ volatile ("movw $0x99, %%ax\n\tmovw %%ax, %%ds" : : : "eax");
#elif defined(TEST_PANIC)
    kprintf("\nTEST : PANIC() dans 2 secondes...\n");
    pit_sleep_ms(2000);
    PANIC("Test de panique volontaire (code %d)", 42);
#elif defined(TEST_ASSERT)
    kprintf("\nTEST : ASSERT() dans 2 secondes...\n");
    pit_sleep_ms(2000);
    ASSERT(pit_ticks() == 0);
#elif defined(TEST_DOUBLEFREE)
    kprintf("\nTEST : double liberation dans 2 secondes...\n");
    pit_sleep_ms(2000);
    void *p = kmalloc(64);
    kfree(p);
    kfree(p);
#elif defined(TEST_HEAPCORRUPT)
    kprintf("\nTEST : debordement de tampon dans 2 secondes...\n");
    pit_sleep_ms(2000);
    u8 *p = kmalloc(32);
    u8 *q = kmalloc(32);
    memset(p, 0x41, 32 + 16 + 4);              /* déborde sur l'en-tête du bloc suivant */
    kfree(q);                                   /* la corruption est détectée ici */
#endif
}

/* Charge chaque module GRUB (archive tar) dans le système de fichiers. */
static void load_initrd(const struct multiboot_info *mbi)
{
    const struct multiboot_module *m = (const struct multiboot_module *)(usize)mbi->mods_addr;
    u32 count = (mbi->flags & MB_FLAG_MODS) ? mbi->mods_count : 0;

    if (count > 16)
        count = 16;
    if (count == 0) {
        status(0, "Pas d'initrd (module GRUB absent) : systeme de fichiers vide");
        return;
    }
    for (u32 i = 0; i < count; i++) {
        struct initrd_stats st;
        err_t e = initrd_load((const u8 *)(usize)m[i].mod_start, m[i].mod_end - m[i].mod_start, &st);

        kprintf("       initrd : %u fichier(s), %u dossier(s), %u octets, %u ignore(s)\n",
                st.files, st.dirs, st.bytes, st.skipped);
        status(e == E_OK, "Initrd charge dans le systeme de fichiers");
        if (e != E_OK)
            klog(LOG_WARN, "initrd : %s", err_str(e));
    }
}

void kernel_main(u32 magic, const struct multiboot_info *mbi)
{
    struct kheap_stats hs;
    err_t e;

    serial_init();
    vga_init();
    stack_guard_init();
    banner();

    kprintf("%s %s - noyau x86 (32 bits)\n\n", PHOENOS_NAME, PHOENOS_VERSION);

    status(1, "Pilote VGA texte et port serie COM1 initialises");

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        PANIC("Magic Multiboot invalide (%#x) : demarrez via GRUB", magic);
    ASSERT(mbi != NULL);
    status(1, "Demarre par un bootloader Multiboot");

    gdt_init();
    status(1, "GDT chargee (segments noyau et utilisateur)");

    idt_init();
    pic_init();
    pit_init(100);

    e = keyboard_init();
    if (e != E_OK)
        PANIC("Initialisation du clavier impossible : %s", err_str(e));

    cpu_sti();
    status(1, "IDT, PIC, timer (100 Hz) et clavier AZERTY initialises");

    __asm__ volatile ("int3");
    status(isr_breakpoint_count() == 1, "Exceptions CPU : point d'arret traite");

    /* --- Mémoire --- */
    pmm_init(mbi);
    kprintf("       Memoire : %u Kio libres pour le noyau (%u frames de 4 Kio)\n",
            pmm_free_frames() * 4, pmm_free_frames());
    status(1, "Gestionnaire de memoire physique initialise");

    kheap_init();
    kprintf("       Tas du noyau : %u Kio\n", kheap_size() / 1024);
    status(1, "Tas du noyau initialise (kmalloc / kfree)");

    status(selftest_pmm(),  "Auto-test memoire physique");
    status(selftest_heap(), "Auto-test du tas (decoupe et fusion des blocs)");

    fs_init();
    status(1, "Systeme de fichiers en RAM initialise");
    load_initrd(mbi);

    /* --- Timer : 300 ms d'attente doivent faire avancer le compteur d'environ 30 ticks --- */
    {
        u32 t0 = pit_ticks();

        pit_sleep_ms(300);
        status(pit_ticks() - t0 >= 25, "Timer PIT fonctionnel (100 Hz)");
    }

    kheap_get_stats(&hs);
    klog(LOG_INFO, "Demarrage termine en %u ms, %u Kio libres dans le tas",
         pit_uptime_ms(), hs.free_bytes / 1024);

    run_selected_test();

    kprintf("\nTapez ");
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    kprintf("help");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf(" pour la liste des commandes.\n");
    {
        struct fs_node *motd;

        if (fs_lookup("/etc/motd", &motd) == E_OK && motd->type == FS_FILE) {
            vga_set_color(VGA_YELLOW, VGA_BLACK);
            for (u32 i = 0; i < motd->size; i++)
                kprintf("%c", motd->data[i]);
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        }
    }
    kprintf("\n");

    shell_run();
}
