#ifndef PHOENOS_KEYBOARD_H
#define PHOENOS_KEYBOARD_H

#include "types.h"
#include "error.h"

/*
 * Clavier PS/2 (IRQ 1), disposition AZERTY français, AltGr géré.
 * Les touches mortes (^ et trema) ne sont pas encore gérées.
 * Les caractères accentués sont renvoyés en CP437 (jeu de l'affichage VGA).
 */
err_t keyboard_init(void);

/* Renvoie un caractère, ou -1 si aucune touche n'est en attente. */
int keyboard_try_getc(void);

/* Attend (en veille) puis renvoie le prochain caractère. */
u8 keyboard_getc(void);

#endif
