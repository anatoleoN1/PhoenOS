#ifndef PHOENOS_SERIAL_H
#define PHOENOS_SERIAL_H

/*
 * Port série COM1 (115200 bauds, 8N1).
 * Tout ce qui est affiché par kprintf y est recopié : avec QEMU
 * (-serial stdio) le journal reste lisible dans le terminal même si
 * l'écran est cassé ou figé.
 */
void serial_init(void);
void serial_putc(char c);
void serial_puts(const char *s);

#endif
