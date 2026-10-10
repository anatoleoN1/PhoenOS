/*
 * PhoenOS - serial.c
 * Pilote minimal du port série COM1 (UART 16550).
 */
#include "serial.h"
#include "io.h"

#define COM1 0x3F8

void serial_init(void)
{
    outb(COM1 + 1, 0x00);   /* pas d'interruptions UART        */
    outb(COM1 + 3, 0x80);   /* DLAB : accès au diviseur        */
    outb(COM1 + 0, 0x01);   /* diviseur = 1 -> 115200 bauds    */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);   /* 8 bits, pas de parité, 1 stop   */
    outb(COM1 + 2, 0xC7);   /* FIFO activée et vidée           */
    outb(COM1 + 4, 0x0B);   /* DTR + RTS + OUT2                */
}

static void serial_raw(char c)
{
    u32 spin = 100000;

    /* Attente bornée : si aucun UART n'est présent, on n'attend pas indéfiniment. */
    while (!(inb(COM1 + 5) & 0x20) && --spin)
        ;
    if (spin)
        outb(COM1, (u8)c);
}

void serial_putc(char c)
{
    if (c == '\n')
        serial_raw('\r');
    serial_raw(c);
}

void serial_puts(const char *s)
{
    while (*s)
        serial_putc(*s++);
}
