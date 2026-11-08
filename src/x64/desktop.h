#ifndef PHOENOS_X64_DESKTOP_H
#define PHOENOS_X64_DESKTOP_H

#include "types.h"
#include "gfx.h"

/*
 * Petit bureau graphique (0.12.0) : fond, quelques fenetres deplacables a la souris (horloge, systeme, dessin, aide),
 * barre des taches. La logique (desk_*) ne touche pas au materiel : elle dessine dans une surface et recoit les evenements,
 * ce qui permet de la tester ; desktop_run() la branche sur l'ecran, la souris et le clavier.
 */
enum { WIN_CLOCK, WIN_SYSTEM, WIN_PAINT, WIN_HELP, WIN_COUNT };

#define PAINT_W 288
#define PAINT_H 176

struct desk_win {
    i32 x, y, w, h;
    int visible;
    const char *title;
};

struct desk {
    struct gfx_surface scr;
    u32 *canvas;                        /* PAINT_W x PAINT_H */
    struct desk_win win[WIN_COUNT];
    int order[WIN_COUNT];               /* de l'arriere vers l'avant */
    int drag;                           /* fenetre deplacee, -1 : aucune */
    i32 dx, dy;
    int painting;
    u32 ink;
    u32 prev_btn;
    i32 pmx, pmy;                       /* dernier point du dessin */
    i32 mx, my;
    int quit;
    /* donnees affichees (fournies par l'appelant) */
    u32 hh, mm, ss, up_s, free_kib;
};

void desk_init(struct desk *d, u32 *screen_px, i32 w, i32 h, u32 *canvas);
void desk_draw(struct desk *d);
void desk_mouse(struct desk *d, i32 x, i32 y, u32 buttons);
void desk_key(struct desk *d, int key);                     /* Echap : quitter */
int  desk_window_at(const struct desk *d, i32 x, i32 y);     /* fenetre visible la plus en avant sous (x, y), -1 sinon */

void desktop_run(void);                                      /* boucle complete sur l'ecran et la souris */

#endif
