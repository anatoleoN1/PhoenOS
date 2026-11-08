#ifndef PHOENOS_MOUSE_H
#define PHOENOS_MOUSE_H

#include "types.h"
#include "error.h"

/*
 * Souris PS/2 (0.12.0, IRQ 12). Le decodeur (mouse_feed) est pur et testable : il assemble les paquets de 3 octets
 * (4 avec la molette, "IntelliMouse") envoyes par la souris, garde la position absolue en pixels entre 0 et les bornes
 * donnees par mouse_set_bounds, et l'etat des boutons. Sous QEMU la souris n'existe pour le systeme que lorsque la
 * fenetre l'a "capturee" : cliquer dans la fenetre (Ctrl+Alt+G pour la liberer).
 */
#define MOUSE_LEFT   1
#define MOUSE_RIGHT  2
#define MOUSE_MIDDLE 4

struct mouse_state {
    i32 x, y;                           /* position en pixels (0 .. bornes - 1) */
    u32 buttons;                        /* MOUSE_LEFT | MOUSE_RIGHT | MOUSE_MIDDLE */
    i32 wheel;                          /* cumul de la molette depuis le dernier mouse_get (haut : negatif) */
    u32 events;                         /* nombre de paquets recus depuis le demarrage (pour detecter un changement) */
};

/* Branche la souris : controleur 8042, IRQ 12. E_NOENT si le controleur ne repond pas / pas de souris. */
err_t mouse_init(void);
int   mouse_present(void);
void  mouse_set_present(int on);         /* pilote (mouse_ps2.c) et tests */              /* 1 si une souris a repondu a l'initialisation */
int   mouse_has_wheel(void);

/* Zone ou la souris se deplace (taille de l'ecran) ; la position est ramenee dedans et recentree si besoin. */
void  mouse_set_bounds(i32 w, i32 h);

/* Etat courant (la molette est remise a zero apres lecture). */
void  mouse_get(struct mouse_state *out);

/* Decodeur : un octet recu de la souris. Renvoie 1 quand il vient de terminer un paquet. */
int   mouse_feed(u8 byte);

/* Choix du format de paquet (3 octets, ou 4 avec molette) et remise a zero du decodeur. */
void  mouse_reset_decoder(int wheel);

#endif
