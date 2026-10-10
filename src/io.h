#ifndef PHOENOS_IO_H
#define PHOENOS_IO_H

#include "types.h"

static inline void outb(u16 port, u8 value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline u8 inb(u16 port)
{
    u8 value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* Petit délai : écriture sur un port inutilisé (0x80). */
static inline void io_wait(void)
{
    outb(0x80, 0);
}

static inline void cpu_cli(void)  { __asm__ volatile ("cli"); }
static inline void cpu_sti(void)  { __asm__ volatile ("sti"); }
static inline void cpu_hlt(void)  { __asm__ volatile ("hlt"); }

#endif
