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

/* Redirection de la sortie (shell : commande > fichier) */
static kprintf_sink_fn sink;
static void *sink_ctx;
static int bypass;                      /* > 0 : keprintf en cours, on ignore la redirection */

void kprintf_redirect(kprintf_sink_fn fn, void *ctx)
{
    sink = fn;
    sink_ctx = ctx;
}

static void out(char c)
{
    if (sink && !bypass) {
        sink(c, sink_ctx);
        return;
    }
    dmesg_buf[dmesg_total++ % DMESG_SIZE] = c;
    vga_putc(c);
    /* Le port série ne transporte que de l'ASCII : les accents CP437 deviennent '?' */
    serial_putc((u8)c >= 0x80 ? '?' : c);
}

static void out_n(char c, usize n)
{
    while (n--)
        out(c);
}

/*
 * Écrit un champ : `prefix` (signe ou 0x) puis `body`, aligné sur `width` colonnes.
 *   left  : remplissage à droite (flag '-')
 *   zero  : zéros entre le préfixe et le corps (flag '0', nombres seulement)
 */
static void emit_field(const char *prefix, const char *body, usize width, int left, int zero)
{
    usize plen = strlen(prefix), blen = strlen(body);
    usize total = plen + blen;
    usize fill = width > total ? width - total : 0;

    if (left) {
        for (usize i = 0; i < plen; i++) out(prefix[i]);
        for (usize i = 0; i < blen; i++) out(body[i]);
        out_n(' ', fill);
    } else if (zero) {
        for (usize i = 0; i < plen; i++) out(prefix[i]);
        out_n('0', fill);
        for (usize i = 0; i < blen; i++) out(body[i]);
    } else {
        out_n(' ', fill);
        for (usize i = 0; i < plen; i++) out(prefix[i]);
        for (usize i = 0; i < blen; i++) out(body[i]);
    }
}

/* Convertit `value` en texte dans buf (buf >= 33 octets), renvoie buf. */
static char *utoa(u32 value, u32 base, int upper, char *buf)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[33];
    usize n = 0, len = 0;

    if (value == 0)
        tmp[n++] = '0';
    while (value) {
        tmp[n++] = digits[value % base];
        value /= base;
    }
    while (n)
        buf[len++] = tmp[--n];
    buf[len] = '\0';
    return buf;
}

void kvprintf(const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        int alt = 0, zero = 0, left = 0;
        usize width = 0;
        char buf[40], chbuf[2];

        if (*fmt != '%') {
            out(*fmt);
            continue;
        }
        fmt++;

        for (;; fmt++) {                        /* drapeaux : # 0 - (dans n'importe quel ordre) */
            if (*fmt == '#')      alt = 1;
            else if (*fmt == '0') zero = 1;
            else if (*fmt == '-') left = 1;
            else break;
        }
        while (*fmt >= '0' && *fmt <= '9') {    /* largeur minimale */
            width = width * 10 + (usize)(*fmt - '0');
            if (width > 80)
                width = 80;
            fmt++;
        }
        if (left)
            zero = 0;

        switch (*fmt) {
        case 's': {
            const char *str = va_arg(ap, const char *);

            emit_field("", str ? str : "(null)", width, left, 0);
            break;
        }
        case 'c':
            chbuf[0] = (char)va_arg(ap, int);
            chbuf[1] = '\0';
            if (chbuf[0] == '\0')
                out(0);                         /* un NUL est affiché tel quel */
            else
                emit_field("", chbuf, width, left, 0);
            break;
        case 'd': {
            i32 v = va_arg(ap, i32);

            if (v >= 0)
                emit_field("", utoa((u32)v, 10, 0, buf), width, left, zero);
            else
                emit_field("-", utoa((u32)(-(v + 1)) + 1u, 10, 0, buf), width, left, zero);
            break;
        }
        case 'u':
            emit_field("", utoa(va_arg(ap, u32), 10, 0, buf), width, left, zero);
            break;
        case 'x':
        case 'X':
            /* Avec '#', le préfixe 0x est TOUJOURS affiché (même pour 0), la largeur l'inclut. */
            emit_field(alt ? "0x" : "", utoa(va_arg(ap, u32), 16, *fmt == 'X', buf),
                       width, left, zero);
            break;
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

void kveprintf(const char *fmt, va_list ap)
{
    bypass++;
    kvprintf(fmt, ap);
    bypass--;
}

void keprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    kveprintf(fmt, ap);
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
