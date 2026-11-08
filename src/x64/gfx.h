#ifndef PHOENOS_X64_GFX_H
#define PHOENOS_X64_GFX_H

#include "types.h"

/*
 * Dessin 2D (0.12.0, edition perf64) sur une surface en memoire : pixels 0x00RRGGBB, une ligne = `stride` pixels.
 * Toutes les operations sont rognees par le rectangle de decoupe (clip) : on ne peut jamais ecrire hors de la surface.
 * L'ecran est affiche a partir d'une surface par fbcon_present() (fbcon.h).
 */
struct gfx_surface {
    u32 *px;
    i32  w, h, stride;
    i32  cx0, cy0, cx1, cy1;            /* decoupe : [cx0, cx1) x [cy0, cy1) */
};

#define GFX_RGB(r, g, b) ((u32)(((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b)))

void gfx_surface_init(struct gfx_surface *s, u32 *px, i32 w, i32 h, i32 stride);   /* decoupe = toute la surface */
void gfx_set_clip(struct gfx_surface *s, i32 x, i32 y, i32 w, i32 h);              /* rogne a la surface */
void gfx_reset_clip(struct gfx_surface *s);

void gfx_fill(struct gfx_surface *s, i32 x, i32 y, i32 w, i32 h, u32 rgb);
void gfx_hline(struct gfx_surface *s, i32 x, i32 y, i32 w, u32 rgb);
void gfx_vline(struct gfx_surface *s, i32 x, i32 y, i32 h, u32 rgb);
void gfx_pixel(struct gfx_surface *s, i32 x, i32 y, u32 rgb);
void gfx_gradient_v(struct gfx_surface *s, i32 x, i32 y, i32 w, i32 h, u32 top, u32 bottom);   /* degrade vertical */

/* Melange `rgb` sur le contenu avec une opacite 0..255 (ombres, voiles). */
void gfx_blend(struct gfx_surface *s, i32 x, i32 y, i32 w, i32 h, u32 rgb, unsigned alpha);

/* Texte en police 8x16, agrandie d'un facteur entier >= 1 ; renvoie la largeur dessinee. fond = -1 : transparent. */
i32  gfx_text(struct gfx_surface *s, i32 x, i32 y, const char *str, u32 fg, i64 bg, unsigned scale);
i32  gfx_text_width(const char *str, unsigned scale);

/* Copie un rectangle d'une surface a une autre (sans transparence), avec agrandissement entier. */
void gfx_blit(struct gfx_surface *dst, i32 x, i32 y, const u32 *src, i32 sw, i32 sh, i32 sstride, unsigned scale);

/* Utilitaires de couleur. */
u32  gfx_mix(u32 a, u32 b, unsigned alpha);        /* alpha 0 : a ; 255 : b */

#endif
