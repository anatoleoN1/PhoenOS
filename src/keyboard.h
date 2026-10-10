#ifndef PHOENOS_KEYBOARD_H
#define PHOENOS_KEYBOARD_H

#include "types.h"
#include "error.h"

/*
 * Clavier PS/2 (IRQ 1), disposition AZERTY français, AltGr géré.
 * Les touches mortes (^ et trema) ne sont pas encore gérées.
 * Les caractères accentués sont renvoyés en CP437 (jeu de l'affichage VGA).
 * Les touches spéciales (flèches...) sont renvoyées avec un code >= 0x100.
 */
#define KEY_UP    0x100
#define KEY_DOWN  0x101
#define KEY_LEFT  0x102
#define KEY_RIGHT 0x103
#define KEY_HOME  0x104
#define KEY_END   0x105
#define KEY_DEL   0x106
#define KEY_PGUP  0x107
#define KEY_PGDN  0x108

err_t keyboard_init(void);

/*
 * Console série : un octet reçu sur COM1 est converti en touche (voir keyboard.c). Appelée par le
 * pilote ; publique pour pouvoir être testée.
 */
void keyboard_feed_serial(u8 byte);

/* Renvoie un caractère (0-255) ou une touche spéciale (KEY_*), ou -1 si rien n'est en attente. */
int keyboard_try_getc(void);

/* Attend (en veille) puis renvoie le prochain caractère ou la prochaine touche spéciale. */
int keyboard_getc(void);

#endif
