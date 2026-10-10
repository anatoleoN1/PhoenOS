/*
 * PhoenOS - kernel.c
 * Point d'entrée du noyau (appelé depuis boot.S).
 */
#include "types.h"
#include "vga.h"
#include "kprintf.h"
#include "io.h"
#include "gdt.h"
#include "isr.h"
#include "pic.h"
#include "pit.h"

#define PHOENOS_VERSION            "0.0.2"
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

#ifdef TEST_FAULT
/* make test-fault : provoque volontairement une division par zéro (#DE). */
static void trigger_fault(void)
{
    __asm__ volatile (
        "xorl %%edx, %%edx\n\t"
        "movl $1, %%eax\n\t"
        "xorl %%ecx, %%ecx\n\t"
        "divl %%ecx"
        : : : "eax", "ecx", "edx");
}
#endif

void kernel_main(u32 magic, const struct multiboot_info *mbi)
{
    vga_init();
    banner();

    kprintf("PhoenOS %s - noyau x86 (32 bits)\n\n", PHOENOS_VERSION);

    status(1, "Pilote VGA texte initialise");

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        status(0, "Magic Multiboot invalide : demarre via GRUB");
        return;
    }
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
    cpu_sti();
    status(1, "IDT, PIC et timer (100 Hz) initialises");

    /* Auto-test de l'IDT : un point d'arret doit être traité sans planter. */
    __asm__ volatile ("int3");
    status(isr_breakpoint_count() == 1, "Exceptions CPU : point d'arret traite");

#ifdef TEST_FAULT
    kprintf("\nTEST : division par zero dans 2 secondes...\n");
    pit_sleep_ms(2000);
    trigger_fault();
#endif

    /* Auto-test du timer : 3 secondes comptées. */
    kprintf("       Timer :");
    for (u32 s = 1; s <= 3; s++) {
        pit_sleep_ms(1000);
        kprintf(" %us", s);
    }
    kprintf("\n");
    status(pit_ticks() >= 300, "Timer PIT fonctionnel");

    kprintf("\nProchaine etape : clavier PS/2, puis gestion de la memoire.\n");

    for (;;)
        cpu_hlt();
}
