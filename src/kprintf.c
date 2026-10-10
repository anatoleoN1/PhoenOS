/*
 * PhoenOS - kprintf.c
 * Mini printf du noyau (sans libc).
 */
#include <stdarg.h>
#include "kprintf.h"
#include "vga.h"

static void put_unsigned(u32 value, u32 base)
{
    static const char digits[] = "0123456789abcdef";
    char buf[32];
    int i = 0;

    if (value == 0)
        buf[i++] = '0';
    while (value) {
        buf[i++] = digits[value % base];
        value /= base;
    }
    while (i--)
        vga_putc(buf[i]);
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            vga_putc(*fmt);
            continue;
        }

        switch (*++fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            vga_puts(s ? s : "(null)");
            break;
        }
        case 'c':
            vga_putc((char)va_arg(ap, int));
            break;
        case 'd': {
            i32 v = va_arg(ap, i32);
            if (v < 0) {
                vga_putc('-');
                put_unsigned((u32)(-(v + 1)) + 1u, 10);
            } else {
                put_unsigned((u32)v, 10);
            }
            break;
        }
        case 'u':
            put_unsigned(va_arg(ap, u32), 10);
            break;
        case 'x':
            vga_puts("0x");
            put_unsigned(va_arg(ap, u32), 16);
            break;
        case '%':
            vga_putc('%');
            break;
        case '\0':
            va_end(ap);
            return;
        default:
            vga_putc('%');
            vga_putc(*fmt);
            break;
        }
    }

    va_end(ap);
}
