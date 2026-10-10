/*
 * PhoenOS - kernel.c
 * Point d'entrée du noyau (appelé depuis boot.S).
 */
#include "types.h"
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

#define PHOENOS_VERSION            "0.0.3"
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002
#define MBI_FLAG_MEM               (1u << 0)

struct multiboot_info {
    u32 flags;
    u32 mem_lower;   /* Kio sous 1 Mio */
    u32 mem_upper;   /* Kio au-dessus de 1 Mio */
    /* le reste n'est pas utilisé pour l'instant */
};

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

/*
 * Tests de panique volontaires : make test-div0 | test-gp | test-panic | test-assert
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
#endif
}

void kernel_main(u32 magic, const struct multiboot_info *mbi)
{
    err_t e;

    serial_init();
    vga_init();
    stack_guard_init();
    banner();

    kprintf("PhoenOS %s - noyau x86 (32 bits)\n\n", PHOENOS_VERSION);

    status(1, "Pilote VGA texte et port serie COM1 initialises");

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        PANIC("Magic Multiboot invalide (%x) : demarrez via GRUB", magic);
    ASSERT(mbi != NULL);
    status(1, "Demarre par un bootloader Multiboot");

    if (mbi->flags & MBI_FLAG_MEM) {
        kprintf("       Memoire : %u Kio (base) + %u Kio (etendue)\n",
                mbi->mem_lower, mbi->mem_upper);
        status(1, "Memoire detectee");
    } else {
        status(0, "Informations memoire indisponibles");
    }

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

    /* Auto-test de l'IDT : un point d'arret doit être traité sans planter. */
    __asm__ volatile ("int3");
    status(isr_breakpoint_count() == 1, "Exceptions CPU : point d'arret traite");

    /* Auto-test du timer : 3 secondes comptées. */
    kprintf("       Timer :");
    for (u32 s = 1; s <= 3; s++) {
        pit_sleep_ms(1000);
        kprintf(" %us", s);
    }
    kprintf("\n");
    status(pit_ticks() >= 300, "Timer PIT fonctionnel");

    klog(LOG_INFO, "Demarrage termine en %u ms", pit_uptime_ms());

    run_selected_test();

    kprintf("\nProchaine etape : gestion de la memoire, puis mini-shell.\n");
    kprintf("Clavier AZERTY pret, tapez du texte :\n> ");

    for (;;) {
        u8 c = keyboard_getc();
        kprintf("%c", c);
        if (c == '\n')
            kprintf("> ");
    }
}
