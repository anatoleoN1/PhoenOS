#ifndef PHOENOS_PANIC_H
#define PHOENOS_PANIC_H

#include "types.h"

/* Pile du noyau (définie dans boot.S) */
extern char stack_bottom[];
extern char stack_top[];

/*
 * PANIC("message %d", x)  : arrête le système avec un écran détaillé
 *                           (message, fichier:ligne, trace d'appel).
 * ASSERT(condition)       : PANIC si la condition est fausse.
 */
void kpanic_at(const char *file, int line, const char *fmt, ...)
    __attribute__((noreturn, format(printf, 3, 4)));

#define PANIC(...)   kpanic_at(__FILE__, __LINE__, __VA_ARGS__)
#define ASSERT(cond) \
    do { if (!(cond)) kpanic_at(__FILE__, __LINE__, "Assertion echouee : %s", #cond); } while (0)

/* Briques réutilisées par l'écran de panique des exceptions CPU (isr.c) */
void panic_begin(const char *title);
void panic_backtrace(u32 ebp, u32 eip);
void panic_end(void) __attribute__((noreturn));

/* Appelé par boot.S si kernel_main() revient. */
void kernel_main_returned(void) __attribute__((noreturn));

/* Détection de débordement de la pile du noyau (canari au bas de la pile). */
void stack_guard_init(void);
void stack_guard_check(void);

#endif
