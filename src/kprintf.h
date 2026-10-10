#ifndef PHOENOS_KPRINTF_H
#define PHOENOS_KPRINTF_H

#include <stdarg.h>

/*
 * Formats : %s %c %d %u %x %%   avec largeur et zéros optionnels : %5d  %05u  %08x
 * %x affiche toujours le préfixe 0x ; la largeur compte les chiffres (%08x -> 0x0000beef).
 * Sortie : écran VGA + port série COM1 (+ tampon dmesg).
 * Le compilateur vérifie les formats (un argument oublié devient une erreur de compilation).
 */
void kvprintf(const char *fmt, va_list ap) __attribute__((format(printf, 1, 0)));
void kprintf(const char *fmt, ...)          __attribute__((format(printf, 1, 2)));

/* Réaffiche tout ce que le noyau a écrit depuis le démarrage (derniers 8 Kio). */
void dmesg_print(void);

#endif
