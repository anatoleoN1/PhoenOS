#ifndef PHOENOS_VGA_H
#define PHOENOS_VGA_H

#include "types.h"

enum vga_color {
    VGA_BLACK = 0, VGA_BLUE, VGA_GREEN, VGA_CYAN,
    VGA_RED, VGA_MAGENTA, VGA_BROWN, VGA_LIGHT_GREY,
    VGA_DARK_GREY, VGA_LIGHT_BLUE, VGA_LIGHT_GREEN, VGA_LIGHT_CYAN,
    VGA_LIGHT_RED, VGA_LIGHT_MAGENTA, VGA_YELLOW, VGA_WHITE
};

void vga_init(void);
void vga_set_color(enum vga_color fg, enum vga_color bg);

/* Redéfinit la couleur RGB (0-255) associée à l'une des 16 couleurs du mode texte. */
void vga_set_palette(enum vga_color c, u8 r, u8 g, u8 b);
void vga_clear(void);
void vga_putc(char c);
void vga_puts(const char *s);

/*
 * Historique de l'écran : les 500 dernières lignes sorties par le haut sont conservées.
 * vga_scroll_view(+n) remonte de n lignes, (-n) redescend ; vga_scroll_reset() revient en bas
 * (et toute écriture le fait aussi). vga_scrollback_lines() : lignes disponibles au-dessus.
 */
void vga_scroll_view(int lines);
void vga_scroll_reset(void);
usize vga_scrollback_lines(void);

#endif
