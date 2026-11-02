#ifndef PHOENOS_X64_FBCON_H
#define PHOENOS_X64_FBCON_H

#include "types.h"

/*
 * Console graphique (edition 64 bits) : la grille de texte 80x25 de vga.c est dessinee en pixels dans le framebuffer
 * fourni par le bootloader (UEFI : GOP ; BIOS : VBE), avec la police 8x16 agrandie d'un facteur entier et centree.
 */

/* Prepare l'ecran a partir de boot_fb. 1 : console graphique prete ; 0 : pas d'ecran graphique utilisable (texte VGA). */
int  fbcon_init(void);

/* Dessine la cellule (ligne y, colonne x) : valeur = code ASCII | (attribut VGA << 8). */
void fbcon_cell(usize y, usize x, u16 value);

/* Place le curseur (trait bas de cellule) ; visible = 0 : le masque. */
void fbcon_cursor(usize y, usize x, int visible);

/* Change la couleur RGB d'une des 16 couleurs VGA ; le prochain dessin d'une cellule utilise la nouvelle couleur. */
void fbcon_palette(unsigned c, u8 r, u8 g, u8 b);

/* Facteur d'agrandissement choisi (pour les messages) et taille de l'ecran. */
unsigned fbcon_scale(void);

extern const u8 font8x16_ascii[95][16];
extern const u8 font8x16_unknown[16];

#endif
