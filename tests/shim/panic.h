/* Remplace src/panic.h pour les tests : PANIC termine le programme de test. */
#ifndef SHIM_PANIC_H
#define SHIM_PANIC_H
#include "types.h"
void test_panic(const char *fmt, ...);
#define PANIC(...)   do { test_panic(__VA_ARGS__); __builtin_unreachable(); } while (0)
#define ASSERT(c)    do { if (!(c)) PANIC("assert " #c); } while (0)
#endif
