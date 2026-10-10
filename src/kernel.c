/*
 * PhoenOS - kernel.c
 * Point d'entrée du noyau (appelé depuis boot.S).
 */
#include "types.h"
#include "vga.h"
#include "kprintf.h"

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

static void ok(const char *msg)
{
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    kprintf("[ OK ] ");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf("%s\n", msg);
}

static void fail(const char *msg)
{
    vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
    kprintf("[FAIL] ");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf("%s\n", msg);
}

void kernel_main(u32 magic, const struct multiboot_info *mbi)
{
    vga_init();
    banner();

    kprintf("PhoenOS %s - noyau x86 (32 bits)\n\n", "0.0.1");

    ok("Pilote VGA texte initialise");

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        fail("Magic Multiboot invalide : demarre via GRUB");
        return;
    }
    ok("Demarre par un bootloader Multiboot");

    if (mbi->flags & MBI_FLAG_MEM) {
        kprintf("       Memoire : %u Kio (base) + %u Kio (etendue)\n",
                mbi->mem_lower, mbi->mem_upper);
        ok("Memoire detectee");
    } else {
        fail("Informations memoire indisponibles");
    }

    kprintf("\nProchaine etape : GDT, IDT, interruptions, clavier.\n");
}
