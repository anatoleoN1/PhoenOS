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

static inline void outw(u16 port, u16 value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline u16 inw(u16 port)
{
    u16 value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outl(u16 port, u32 value)
{
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline u32 inl(u16 port)
{
    u32 value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
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
/* sti puis hlt, SANS interruption possible entre les deux (sinon un reveil pourrait etre rate). */
static inline void cpu_sti_hlt(void) { __asm__ volatile ("sti; hlt" : : : "memory"); }

/* Section critique : coupe les interruptions et renvoie l'état précédent. */
static inline u32 cpu_irq_save(void)
{
    u32 flags;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void cpu_irq_restore(u32 flags)
{
    __asm__ volatile ("push %0; popf" : : "r"(flags) : "memory", "cc");
}

#endif
