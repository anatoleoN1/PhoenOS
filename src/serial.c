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

/* Lettres accentuées de l'affichage (CP437) -> UTF-8, pour qu'un terminal les affiche correctement. */
static const struct { u8 cp437; const char *utf8; } accents[] = {
    { 0x80, "\xC3\x87" }, { 0x81, "\xC3\xBC" }, { 0x82, "\xC3\xA9" }, { 0x83, "\xC3\xA2" },
    { 0x84, "\xC3\xA4" }, { 0x85, "\xC3\xA0" }, { 0x87, "\xC3\xA7" }, { 0x88, "\xC3\xAA" },
    { 0x89, "\xC3\xAB" }, { 0x8A, "\xC3\xA8" }, { 0x8B, "\xC3\xAF" }, { 0x8C, "\xC3\xAE" },
    { 0x93, "\xC3\xB4" }, { 0x94, "\xC3\xB6" }, { 0x96, "\xC3\xBB" }, { 0x97, "\xC3\xB9" },
    { 0xF8, "\xC2\xB0" },
};

void serial_putc(char c)
{
    if ((u8)c >= 0x80) {
        for (usize i = 0; i < sizeof(accents) / sizeof(accents[0]); i++) {
            if (accents[i].cp437 == (u8)c) {
                for (const char *p = accents[i].utf8; *p; p++)
                    serial_raw(*p);
                return;
            }
        }
        serial_raw('?');                        /* autre caractère CP437 : pas d'équivalent simple */
        return;
    }
    if (c == '\n')
        serial_raw('\r');
    serial_raw(c);
}

int serial_try_getc(void)
{
    u8 lsr = inb(COM1 + 5);

    if (lsr == 0xFF || !(lsr & 0x01))           /* 0xFF : pas d'UART sur le bus ; bit 0 : octet reçu */
        return -1;
    return inb(COM1);
}

void serial_puts(const char *s)
{
    while (*s)
        serial_putc(*s++);
}
