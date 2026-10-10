/*
 * PhoenOS - kprintf.c
 * Mini printf du noyau (sans libc). Écrit à l'écran ET sur le port série.
 */
#include "kprintf.h"
#include "vga.h"
#include "serial.h"
#include "string.h"

/* Anneau qui garde les derniers 8 Kio affichés : commande `dmesg` du shell. */
#define DMESG_SIZE 8192
static char dmesg_buf[DMESG_SIZE];
static u32  dmesg_total;                /* nombre d'octets écrits depuis le démarrage */

static void out(char c)
{
    dmesg_buf[dmesg_total++ % DMESG_SIZE] = c;
    vga_putc(c);
    /* Le port série ne transporte que de l'ASCII : les accents CP437 deviennent '?' */
    serial_putc((u8)c >= 0x80 ? '?' : c);
}

/* Écrit `len` caractères de `s`, précédés de remplissage pour atteindre `width`. */
static void put_padded(const char *s, usize len, usize width, char pad)
{
    while (width > len) {
        out(pad);
        width--;
    }
    for (usize i = 0; i < len; i++)
        out(s[i]);
}

/* Convertit `value` en texte dans buf (renvoie la longueur ; buf >= 32 octets). */
static usize utoa(u32 value, u32 base, char *buf)
{
    static const char digits[] = "0123456789abcdef";
    char tmp[32];
    usize n = 0, len = 0;

    if (value == 0)
        tmp[n++] = '0';
    while (value) {
        tmp[n++] = digits[value % base];
        value /= base;
    }
    while (n)
        buf[len++] = tmp[--n];
    return len;
}

void kvprintf(const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        char pad = ' ';
        usize width = 0;
        char buf[32];

        if (*fmt != '%') {
            out(*fmt);
            continue;
        }
        fmt++;

        if (*fmt == '0') {                      /* %05d : remplissage par des zéros */
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {    /* largeur minimale */
            width = width * 10 + (usize)(*fmt - '0');
            if (width > 80)
                width = 80;
            fmt++;
        }

        switch (*fmt) {
        case 's': {
            const char *str = va_arg(ap, const char *);

            if (!str)
                str = "(null)";
            put_padded(str, strlen(str), width, ' ');
            break;
        }
        case 'c':
            buf[0] = (char)va_arg(ap, int);
            put_padded(buf, 1, width, ' ');
            break;
        case 'd': {
            i32 v = va_arg(ap, i32);
            usize len;

            if (v >= 0) {
                len = utoa((u32)v, 10, buf);
                put_padded(buf, len, width, pad);
            } else if (pad == '0') {            /* -07 : le signe d'abord, puis les zéros */
                out('-');
                len = utoa((u32)(-(v + 1)) + 1u, 10, buf);
                put_padded(buf, len, width > 0 ? width - 1 : 0, '0');
            } else {                            /*  -7 : les espaces d'abord, puis le signe */
                buf[0] = '-';
                len = utoa((u32)(-(v + 1)) + 1u, 10, buf + 1);
                put_padded(buf, len + 1, width, ' ');
            }
            break;
        }
        case 'u': {
            usize len = utoa(va_arg(ap, u32), 10, buf);

            put_padded(buf, len, width, pad);
            break;
        }
        case 'x': {                             /* toujours précédé de 0x ; la largeur compte les chiffres */
            usize len = utoa(va_arg(ap, u32), 16, buf);

            out('0');
            out('x');
            put_padded(buf, len, width, '0');
            break;
        }
        case '%':
            out('%');
            break;
        case '\0':                              /* '%' isolé en fin de texte : affiché tel quel */
            out('%');
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

void dmesg_print(void)
{
    u32 n     = dmesg_total < DMESG_SIZE ? dmesg_total : DMESG_SIZE;
    u32 start = dmesg_total - n;

    /* Écriture directe (sans passer par out()) pour ne pas se recopier soi-même */
    for (u32 i = 0; i < n; i++) {
        char c = dmesg_buf[(start + i) % DMESG_SIZE];

        vga_putc(c);
        serial_putc((u8)c >= 0x80 ? '?' : c);
    }
}
