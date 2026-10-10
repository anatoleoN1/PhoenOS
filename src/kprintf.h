#ifndef PHOENOS_KPRINTF_H
#define PHOENOS_KPRINTF_H

#include <stdarg.h>

/*
 * Formats : %s %c %d %u %x (affiche 0x...) %%
 * Sortie : écran VGA + port série COM1.
 */
void kvprintf(const char *fmt, va_list ap);
void kprintf(const char *fmt, ...);

#endif
