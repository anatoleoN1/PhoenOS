/* Remplace src/io.h pour les tests : aucune instruction privilégiée. */
#ifndef SHIM_IO_H
#define SHIM_IO_H
#include "types.h"

extern u8 test_scancode;                     /* octet "lu" sur le port 0x60 */

/*
 * Les accès aux ports passent par ces deux fonctions. Par défaut (définitions faibles dans testlib.h),
 * seul le port 0x60 répond ; un test peut fournir ses propres versions pour simuler du matériel
 * (voir fakeata.h : faux disque ATA).
 */
u32 test_port_in(u16 port, int size);
void test_port_out(u16 port, u32 v, int size);

static inline u8  inb(u16 port)              { return (u8)test_port_in(port, 1); }
static inline u16 inw(u16 port)              { return (u16)test_port_in(port, 2); }
static inline void outb(u16 port, u8 v)      { test_port_out(port, v, 1); }
static inline void outw(u16 port, u16 v)     { test_port_out(port, v, 2); }
static inline void io_wait(void)             { }
static inline void cpu_cli(void)             { }
static inline void cpu_sti(void)             { }
static inline void cpu_hlt(void)             { }
static inline u32  cpu_irq_save(void)        { return 0; }
static inline void cpu_irq_restore(u32 f)    { (void)f; }
#endif
