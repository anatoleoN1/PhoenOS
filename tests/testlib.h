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

static inline long sys3(long n, long a, long b, long c)
{
    long r;

    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return r;
}

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

static int test_fails;

#define CHECK(c) do { \
    if (!(c)) { out("  ECHEC ligne "); outnum(__LINE__); out(" : " #c "\n"); test_fails++; } \
} while (0)

int test_main(void);

void _start(void)
{
    int r = test_main();

    exit_now(r + test_fails);
}

#endif
