/* Test du vrai kprintf.c : formats, largeur, zéros, cas limites. */
#pragma GCC diagnostic ignored "-Wformat"
#pragma GCC diagnostic ignored "-Wformat-extra-args"
#pragma GCC diagnostic ignored "-Wformat-overflow"
#include "testlib.h"
#include "kprintf.h"
#include "vga.h"
#include "serial.h"

u8 test_scancode;

static char cap[512];
static usize caplen;

void vga_putc(char c) { if (caplen + 1 < sizeof cap) cap[caplen++] = c; }
void serial_putc(char c) { (void)c; }
void vga_set_color(enum vga_color fg, enum vga_color bg) { (void)fg; (void)bg; }
void vga_clear(void) { }

#define EXPECT(expected, ...) do { \
    caplen = 0; kprintf(__VA_ARGS__); cap[caplen] = 0; \
    if (strcmp(cap, expected)) { \
        out("  ECHEC ligne "); outnum(__LINE__); out(" : attendu [" expected "] obtenu ["); out(cap); out("]\n"); \
        test_fails++; \
    } \
} while (0)

int test_main(void)
{
    EXPECT("abc", "abc");
    EXPECT("42", "%d", 42);
    EXPECT("-42", "%d", -42);
    EXPECT("0", "%d", 0);
    EXPECT("-2147483648", "%d", (int)0x80000000);                /* INT_MIN */
    EXPECT("2147483647", "%d", 2147483647);
    EXPECT("4294967295", "%u", 0xFFFFFFFFu);
    EXPECT("beef", "%x", 0xBEEFu);
    EXPECT("0", "%x", 0u);
    EXPECT("ffffffff", "%x", 0xFFFFFFFFu);
    EXPECT("BEEF", "%X", 0xBEEFu);
    EXPECT("0xbeef", "%#x", 0xBEEFu);
    EXPECT("0x0", "%#x", 0u);
    EXPECT("0x0000beef", "%#010x", 0xBEEFu);
    EXPECT("0xBEEF", "%#X", 0xBEEFu);
    EXPECT("hi", "%s", "hi");
    EXPECT("(null)", "%s", (char *)0);
    EXPECT("x", "%c", 'x');
    EXPECT("100%", "100%%");

    /* largeur et zéros (régression : `%2d` faisait lire l'argument suivant de travers) */
    EXPECT("  1  help", " %2d  %s", 1, "help");
    EXPECT("12  bye", "%2d  %s", 12, "bye");
    EXPECT("  7", "%3d", 7);
    EXPECT("123", "%2d", 123);                                   /* plus large que la largeur : pas tronqué */
    EXPECT("007", "%03d", 7);
    EXPECT("-07", "%03d", -7);
    EXPECT("  -7", "%4d", -7);
    EXPECT("-12345", "%3d", -12345);
    EXPECT("-2147483648", "%5d", (int)0x80000000);
    EXPECT("00042", "%05u", 42u);
    EXPECT("0000beef", "%08x", 0xBEEFu);
    EXPECT("ab  |", "%-4s|", "ab");
    EXPECT("7   |", "%-4d|", 7);
    EXPECT("  ab|", "%4s|", "ab");
    EXPECT("  ab", "%4s", "ab");
    EXPECT("ab", "%1s", "ab");
    EXPECT("  x", "%3c", 'x');
    EXPECT("a=1 b=2 c=0x3", "a=%d b=%u c=%#x", 1, 2u, 3u);

    /* cas anormaux : ne doivent ni planter ni consommer d'argument */
    EXPECT("%q", "%q");
    EXPECT("fin%", "fin%");
    EXPECT("1", "%d", 1);

    out("  kprintf verifie : formats, largeur, zeros, cas limites\n");
    return 0;
}
