/*
 * PhoenOS - kprintf.c
 * Mini printf du noyau (sans libc). Écrit à l'écran ET sur le port série.
 */
#include "kprintf.h"
#include "vga.h"
#include "serial.h"

static void out(char c)
{
    vga_putc(c);
    /* Le port série ne transporte que de l'ASCII : les accents CP437 deviennent '?' */
    serial_putc((u8)c >= 0x80 ? '?' : c);
}

static void out_str(const char *s)
{
    while (*s)
        out(*s++);
}

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
        out(buf[i]);
}

void kvprintf(const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            out(*fmt);
            continue;
        }

        switch (*++fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            out_str(s ? s : "(null)");
            break;
        }
        case 'c':
            out((char)va_arg(ap, int));
            break;
        case 'd': {
            i32 v = va_arg(ap, i32);
            if (v < 0) {
                out('-');
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
            out_str("0x");
            put_unsigned(va_arg(ap, u32), 16);
            break;
        case '%':
            out('%');
            break;
        case '\0':
            return;
        default:
            out('%');
            out(*fmt);
            break;
        }
    }
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
}
