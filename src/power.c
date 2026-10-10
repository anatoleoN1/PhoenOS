/*
 * PhoenOS - power.c
 * Redémarrage et arrêt.
 */
#include "power.h"
#include "io.h"
#include "kprintf.h"

void power_reboot(void)
{
    u32 spin = 100000;

    cpu_cli();

    /* 1) Impulsion de reset via le contrôleur clavier (8042) */
    while ((inb(0x64) & 0x02) && --spin)
        ;
    outb(0x64, 0xFE);
    for (u32 i = 0; i < 1000000; i++)
        io_wait();

    /* 2) Dernier recours : IDT vide + exception = triple faute = reset du CPU */
    static const struct { u16 limit; u32 base; } __attribute__((packed)) empty_idt = { 0, 0 };
    __asm__ volatile ("lidt %0; int3" : : "m"(empty_idt));

    for (;;)
        cpu_hlt();
}

void power_halt(void)
{
    cpu_cli();
    for (;;)
        cpu_hlt();
}

void power_shutdown(void)
{
    cpu_cli();

    outw(0x604,  0x2000);       /* QEMU (machines récentes)  */
    outw(0xB004, 0x2000);       /* QEMU / Bochs (anciens)    */
    outw(0x4004, 0x3400);       /* VirtualBox                */

    kprintf("\nArret impossible automatiquement sur cette machine.\n");
    kprintf("Vous pouvez eteindre l'ordinateur.\n");
    power_halt();
}
