/* Aides graphiques communes (en-tete seul) : ouverture de l'ecran, choix de l'echelle, centrage. */
#ifndef GFXLIB_H
#define GFXLIB_H
#include "phoenos.h"

#define RGB(r, g, b) ((((uint)(r) & 255) << 16) | (((uint)(g) & 255) << 8) | ((uint)(b) & 255))

/* Prend l'ecran et calcule echelle + origine pour afficher w x h centre. Renvoie 0, ou le code d'erreur apres un message. */
static inline int gfx_open(uint w, uint h, uint *scale, uint *ox, uint *oy)
{
    uint sw, sh, s;
    int r = gfx_info(&sw, &sh);

    if (r != 0) {
        puts("Pas d'ecran graphique (edition perf64 avec framebuffer, par exemple QEMU -vga std).\n");
        return r;
    }
    r = gfx_claim(1);
    if (r != 0) {
        puts("Impossible de prendre l'ecran.\n");
        return r;
    }
    s = sw / w < sh / h ? sw / w : sh / h;
    if (s < 1) s = 1;
    if (s > 8) s = 8;
    *scale = s;
    *ox = sw > w * s ? (sw - w * s) / 2 : 0;
    *oy = sh > h * s ? (sh - h * s) / 2 : 0;
    return 0;
}
#endif
