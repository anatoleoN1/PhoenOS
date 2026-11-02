/*
 * PhoenOS - tests/testlib.h
 * Mini-bibliothèque de test : programme 32 bits autonome (sans libc), qui
 * utilise directement les appels système Linux write/exit.
 * Chaque test définit `int test_main(void)` et renvoie le nombre d'échecs.
 */
#ifndef PHOENOS_TESTLIB_H
#define PHOENOS_TESTLIB_H

#include "types.h"
#include "string.h"

#define EXIT_EXPECTED_PANIC 100     /* code de sortie : "panique attendue" */

#ifdef __x86_64__
/* tests 64 bits : appels Linux x86-64 (write = 1, exit = 60) ; les numeros i386 write/exit sont traduits */
static inline long sys3(long n, long a, long b, long c)
{
    long r;

    n = (n == 4) ? 1 : (n == 1) ? 60 : n;
    __asm__ volatile ("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
    return r;
}
#else
static inline long sys3(long n, long a, long b, long c)
{
    long r;

    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return r;
}
#endif

static inline void out(const char *s) { sys3(4, 1, (long)s, (long)strlen(s)); }
static inline void exit_now(int code) __attribute__((noreturn));
static inline void exit_now(int code) { sys3(1, code, 0, 0); for (;;) ; }

static inline void outnum(u32 v)
{
    char b[12];
    int i = 11;

    b[i] = 0;
    if (!v)
        b[--i] = '0';
    while (v) {
        b[--i] = (char)('0' + v % 10);
        v /= 10;
    }
    out(b + i);
}

extern u8 test_scancode;
#ifndef TEST_HAS_PORTS                      /* fakeata.h fournit ses propres versions */
__attribute__((weak)) u32 test_port_in(u16 port, int size) { (void)size; return port == 0x60 ? test_scancode : 0; }
__attribute__((weak)) void test_port_out(u16 port, u32 v, int size) { (void)port; (void)v; (void)size; }
#endif

static int test_fails;

#define CHECK(c) do { \
    if (!(c)) { out("  ECHEC ligne "); outnum(__LINE__); out(" : " #c "\n"); test_fails++; } \
} while (0)

int test_main(void);

#ifdef __x86_64__
__attribute__((force_align_arg_pointer))
#endif
void _start(void)
{
    int r = test_main();

    exit_now(r + test_fails);
}

#endif
