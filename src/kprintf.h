#ifndef PHOENOS_KPRINTF_H
#define PHOENOS_KPRINTF_H

#include <stdarg.h>
#include "types.h"

/*
 * Formats : %s %c %d %u %x %X %%
 * Drapeaux : '#' (préfixe 0x pour %x, toujours affiché), '0' (zéros), '-' (aligné à gauche)
 * Largeur : %5d  %05u  %#010x (0x0000beef)  %-12s
 * Sortie : écran VGA + port série COM1 (+ tampon dmesg).
 * Le compilateur vérifie les formats (un argument oublié devient une erreur de compilation).
 */
void kvprintf(const char *fmt, va_list ap) __attribute__((format(printf, 1, 0)));
void kprintf(const char *fmt, ...)          __attribute__((format(printf, 1, 2)));

/* Formate dans un tampon (sans ecran ni port serie) ; reentrant, donc utilisable dans une interruption. */
usize kvsnprintf(char *buf, usize n, const char *fmt, va_list ap) __attribute__((format(printf, 3, 0)));
usize ksnprintf(char *buf, usize n, const char *fmt, ...)         __attribute__((format(printf, 3, 4)));

/* Comme kprintf, mais ignore la redirection : messages d'erreur qui doivent rester à l'écran. */
void kveprintf(const char *fmt, va_list ap) __attribute__((format(printf, 1, 0)));
void keprintf(const char *fmt, ...)         __attribute__((format(printf, 1, 2)));

/*
 * Redirige tout ce qu'écrit kprintf vers `fn` (au lieu de l'écran), jusqu'à
 * kprintf_redirect(NULL, NULL). Utilisé par le shell pour `commande > fichier`.
 */
typedef void (*kprintf_sink_fn)(char c, void *ctx);
void kprintf_redirect(kprintf_sink_fn fn, void *ctx);

/* Réaffiche tout ce que le noyau a écrit depuis le démarrage (derniers 8 Kio). */
void dmesg_print(void);

#endif
