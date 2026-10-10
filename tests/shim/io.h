/* Remplace src/io.h pour les tests : aucune instruction privilégiée. */
#ifndef SHIM_IO_H
#define SHIM_IO_H
#include "types.h"

extern u8 test_scancode;                     /* octet "lu" sur le port 0x60 */

static inline u8 inb(u16 port)               { return port == 0x60 ? test_scancode : 0; }
static inline void outb(u16 port, u8 v)      { (void)port; (void)v; }
static inline void outw(u16 port, u16 v)     { (void)port; (void)v; }
static inline void io_wait(void)             { }
static inline void cpu_cli(void)             { }
static inline void cpu_sti(void)             { }
static inline void cpu_hlt(void)             { }
static inline u32  cpu_irq_save(void)        { return 0; }
static inline void cpu_irq_restore(u32 f)    { (void)f; }
#endif
