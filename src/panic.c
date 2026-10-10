/*
 * PhoenOS - panic.c
 * Écran de panique, trace d'appel, assertions, garde de pile.
 * Tout est aussi écrit sur le port série (kprintf), donc visible dans le
 * terminal avec `make run` / `make serial`.
 */
#include <stdarg.h>
#include "panic.h"
#include "kprintf.h"
#include "serial.h"
#include "vga.h"
#include "io.h"

#define STACK_CANARY 0xC0FFEE42u

static volatile int panicking;

void panic_begin(const char *title)
{
    cpu_cli();

    if (panicking) {
        /* Panique pendant une panique : on n'essaie plus rien de compliqué. */
        serial_puts("\n*** PANIQUE RECURSIVE : arret ***\n");
        for (;;)
            cpu_hlt();
    }
    panicking = 1;

    vga_set_color(VGA_WHITE, VGA_RED);
    vga_clear();
    kprintf("\n  PhoenOS - PANIQUE NOYAU\n  %s\n\n", title);
}

void panic_backtrace(u32 ebp, u32 eip)
{
    int n = 0;

    kprintf("\n  Trace d'appel (decoder avec : make symbol ADDR=<adresse>) :\n");

    if (eip)
        kprintf("   #%d  %x  <- instruction courante\n", n++, eip);

    /* Remontée de la chaîne des frames (-fno-omit-frame-pointer) */
    while (n < 12) {
        u32 *frame;

        if ((ebp & 3) || ebp < (u32)stack_bottom || ebp + 8 > (u32)stack_top)
            break;
        frame = (u32 *)ebp;
        if (frame[1] == 0)
            break;
        kprintf("   #%d  %x\n", n++, frame[1]);
        if (frame[0] <= ebp)
            break;
        ebp = frame[0];
    }

    if (n == 0)
        kprintf("   (indisponible)\n");
}

void panic_end(void)
{
    kprintf("\n  Systeme arrete. Redemarrez la machine.\n");
    for (;;) {
        cpu_cli();
        cpu_hlt();
    }
}

void kpanic_at(const char *file, int line, const char *fmt, ...)
{
    va_list ap;

    panic_begin("Erreur fatale du noyau");

    kprintf("  Message : ");
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    kprintf("\n  Lieu    : %s:%d\n", file, line);

    panic_backtrace((u32)__builtin_frame_address(0), 0);
    panic_end();
}

void kernel_main_returned(void)
{
    PANIC("kernel_main() a rendu la main au bootloader");
}

void stack_guard_init(void)
{
    *(volatile u32 *)stack_bottom = STACK_CANARY;
}

void stack_guard_check(void)
{
    if (*(volatile u32 *)stack_bottom != STACK_CANARY)
        PANIC("Debordement de la pile du noyau (garde ecrasee)");
}
