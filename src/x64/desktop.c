/*
 * PhoenOS - x64/desktop.c
 * Bureau graphique : fenetres deplacables, barre des taches, petit programme de dessin.
 */
#include "desktop.h"
#include "mouse.h"
#include "kprintf.h"
#include "string.h"

/* Palette : ardoise sombre, papier chaud, un seul accent. */
#define C_BG_TOP   GFX_RGB(0x1B, 0x26, 0x31)
#define C_BG_BOT   GFX_RGB(0x34, 0x4B, 0x5C)
#define C_PAPER    GFX_RGB(0xEB, 0xE8, 0xE1)
#define C_INK      GFX_RGB(0x22, 0x29, 0x30)
#define C_MUTED    GFX_RGB(0x6C, 0x74, 0x7B)
#define C_TITLE    GFX_RGB(0x26, 0x33, 0x40)
#define C_TITLE_I  GFX_RGB(0x58, 0x63, 0x6E)
#define C_ACCENT   GFX_RGB(0xDB, 0x84, 0x2E)
#define C_BAR      GFX_RGB(0x12, 0x18, 0x1E)
#define BAR_H      32
#define TITLE_H    24

static const u32 inks[] = { GFX_RGB(0x22, 0x29, 0x30), GFX_RGB(0xC2, 0x4B, 0x3A), GFX_RGB(0xDB, 0x84, 0x2E),
                            GFX_RGB(0x3F, 0x8F, 0x6B), GFX_RGB(0x2F, 0x6F, 0xB0), GFX_RGB(0x7A, 0x4F, 0x9E) };
#define NINK ((int)(sizeof inks / sizeof inks[0]))

/* Flèche de la souris : 12 x 19, 1 = noir, 2 = blanc. */
static const char *const arrow[19] = {
    "1           ", "11          ", "121         ", "1221        ", "12221       ", "122221      ", "1222221     ",
    "12222221    ", "122222221   ", "1222222221  ", "12222222221 ", "122222111111", "1221222 1    ", "121 1222 1   ",
    "11  1222 1   ", "1    12221   ", "     1222 1  ", "      111    ", "             "
};

int desk_window_at(const struct desk *d, i32 x, i32 y)
{
    for (int i = WIN_COUNT - 1; i >= 0; i--) {
        const struct desk_win *w = &d->win[d->order[i]];

        if (w->visible && x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h)
            return d->order[i];
    }
    return -1;
}

static void raise_win(struct desk *d, int id)
{
    int k = 0;

    while (d->order[k] != id)
        k++;
    for (; k < WIN_COUNT - 1; k++)
        d->order[k] = d->order[k + 1];
    d->order[WIN_COUNT - 1] = id;
}

void desk_init(struct desk *d, u32 *px, i32 w, i32 h, u32 *canvas)
{
    static const char *const titles[WIN_COUNT] = { "Horloge", "Systeme", "Dessin", "Aide" };
    static const i32 sz[WIN_COUNT][2] = { { 232, 112 }, { 280, 128 }, { PAINT_W + 24, PAINT_H + TITLE_H + 52 }, { 340, 152 } };

    memset(d, 0, sizeof *d);
    gfx_surface_init(&d->scr, px, w, h, w);
    d->canvas = canvas;
    for (int i = 0; i < PAINT_W * PAINT_H; i++)
        canvas[i] = C_PAPER;
    for (int i = 0; i < WIN_COUNT; i++) {
        d->win[i].w = sz[i][0];
        d->win[i].h = sz[i][1];
        d->win[i].title = titles[i];
        d->win[i].visible = 1;
        d->win[i].x = 40 + i * 56;
        d->win[i].y = 40 + i * 44;
        if (d->win[i].x + d->win[i].w > w)
            d->win[i].x = w > d->win[i].w ? w - d->win[i].w : 0;
        if (d->win[i].y + d->win[i].h > h - BAR_H)
            d->win[i].y = h - BAR_H > d->win[i].h ? h - BAR_H - d->win[i].h : 0;
        d->order[i] = i;
    }
    d->drag = -1;
    d->ink = inks[0];
    d->mx = w / 2;
    d->my = h / 2;
}

static void put2(char *s, u32 v) { s[0] = '0' + (v / 10) % 10; s[1] = '0' + v % 10; }

static void draw_window(struct desk *d, int id)
{
    struct gfx_surface *s = &d->scr;
    struct desk_win *w = &d->win[id];
    int active = d->order[WIN_COUNT - 1] == id;
    i32 bx = w->x + 16, by = w->y + TITLE_H + 14;
    char t[48];

    gfx_blend(s, w->x + 6, w->y + 8, w->w, w->h, 0x000000, 70);            /* ombre portee */
    gfx_fill(s, w->x, w->y, w->w, w->h, C_PAPER);
    gfx_fill(s, w->x, w->y, w->w, TITLE_H, active ? C_TITLE : C_TITLE_I);
    gfx_text(s, w->x + 12, w->y + 4, w->title, GFX_RGB(0xF2, 0xEF, 0xE8), -1, 1);
    gfx_fill(s, w->x + w->w - 22, w->y + 6, 12, 12, active ? C_ACCENT : C_MUTED);   /* fermer */

    switch (id) {
    case WIN_CLOCK:
        t[0] = 0;
        ksnprintf(t, sizeof t, "%c%c:%c%c:%c%c", '0' + d->hh / 10, '0' + d->hh % 10, '0' + d->mm / 10, '0' + d->mm % 10,
                  '0' + d->ss / 10, '0' + d->ss % 10);
        gfx_text(s, bx, by + 4, t, C_INK, -1, 4);
        gfx_text(s, bx, by + 4 + 64, "heure du systeme", C_MUTED, -1, 1);
        break;
    case WIN_SYSTEM:
        ksnprintf(t, sizeof t, "PhoenOS - %d x %d", s->w, s->h);
        gfx_text(s, bx, by, t, C_INK, -1, 1);
        ksnprintf(t, sizeof t, "Allume depuis %u s", d->up_s);
        gfx_text(s, bx, by + 24, t, C_INK, -1, 1);
        ksnprintf(t, sizeof t, "Memoire libre : %u Kio", d->free_kib);
        gfx_text(s, bx, by + 48, t, C_INK, -1, 1);
        gfx_text(s, bx, by + 72, "Souris PS/2, framebuffer 32 bits", C_MUTED, -1, 1);
        break;
    case WIN_PAINT: {
        i32 cx = w->x + 12, cy = w->y + TITLE_H + 10;

        gfx_blit(s, cx, cy, d->canvas, PAINT_W, PAINT_H, PAINT_W, 1);
        for (int i = 0; i < NINK; i++) {
            i32 px = cx + i * 32;

            gfx_fill(s, px, cy + PAINT_H + 12, 24, 16, inks[i]);
            if (inks[i] == d->ink)
                gfx_fill(s, px, cy + PAINT_H + 32, 24, 3, C_ACCENT);
        }
        gfx_text(s, cx + 6 * 32 + 8, cy + PAINT_H + 12, "clic droit : effacer", C_MUTED, -1, 1);
        break;
    }
    default:
        gfx_text(s, bx, by, "Glisser la barre de titre", C_INK, -1, 1);
        gfx_text(s, bx, by + 22, "pour deplacer une fenetre.", C_INK, -1, 1);
        gfx_text(s, bx, by + 50, "Carre orange : fermer.", C_INK, -1, 1);
        gfx_text(s, bx, by + 72, "Barre du bas : rouvrir.", C_INK, -1, 1);
        gfx_text(s, bx, by + 100, "Echap : retour au shell.", C_MUTED, -1, 1);
        break;
    }
}

void desk_draw(struct desk *d)
{
    struct gfx_surface *s = &d->scr;
    i32 bar = s->h - BAR_H, x = 96;
    char t[12];

    gfx_gradient_v(s, 0, 0, s->w, bar, C_BG_TOP, C_BG_BOT);
    for (int i = 0; i < WIN_COUNT; i++)
        if (d->win[d->order[i]].visible)
            draw_window(d, d->order[i]);
    gfx_fill(s, 0, bar, s->w, BAR_H, C_BAR);
    gfx_fill(s, 0, bar, 80, BAR_H, C_ACCENT);
    gfx_text(s, 10, bar + 8, "PhoenOS", C_BAR, -1, 1);
    for (int i = 0; i < WIN_COUNT; i++) {
        i32 tw = gfx_text_width(d->win[i].title, 1) + 20;

        gfx_fill(s, x, bar + 4, tw, BAR_H - 8, d->win[i].visible ? C_TITLE : C_BAR);
        gfx_text(s, x + 10, bar + 8, d->win[i].title, d->win[i].visible ? GFX_RGB(0xF2, 0xEF, 0xE8) : C_MUTED, -1, 1);
        x += tw + 6;
    }
    memcpy(t, "00:00:00", 9);
    put2(t, d->hh); put2(t + 3, d->mm); put2(t + 6, d->ss);
    gfx_text(s, s->w - 80, bar + 8, t, GFX_RGB(0xF2, 0xEF, 0xE8), -1, 1);
    for (int y = 0; y < 19; y++)
        for (int xx = 0; xx < 12; xx++)
            if (arrow[y][xx] != ' ' && arrow[y][xx] != '\0')
                gfx_pixel(s, d->mx + xx, d->my + y, arrow[y][xx] == '1' ? 0x000000 : 0xFFFFFF);
}

static void canvas_dot(struct desk *d, i32 x, i32 y)
{
    for (i32 j = -1; j <= 1; j++)
        for (i32 i = -1; i <= 1; i++)
            if (x + i >= 0 && x + i < PAINT_W && y + j >= 0 && y + j < PAINT_H)
                d->canvas[(y + j) * PAINT_W + x + i] = d->ink;
}

static void canvas_line(struct desk *d, i32 x0, i32 y0, i32 x1, i32 y1)
{
    i32 dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    i32 sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;

    for (int n = 0; n < 4096; n++) {
        canvas_dot(d, x0, y0);
        if (x0 == x1 && y0 == y1)
            break;
        if (2 * err >= dy) { err += dy; x0 += sx; }
        if (2 * err <= dx) { err += dx; y0 += sy; }
    }
}

void desk_mouse(struct desk *d, i32 x, i32 y, u32 btn)
{
    int pressed = (btn & MOUSE_LEFT) && !(d->prev_btn & MOUSE_LEFT);
    int right = (btn & MOUSE_RIGHT) && !(d->prev_btn & MOUSE_RIGHT);
    i32 bar = d->scr.h - BAR_H;

    d->mx = x;
    d->my = y;
    if (!(btn & MOUSE_LEFT)) {
        d->drag = -1;
        d->painting = 0;
    }
    if (d->drag >= 0) {
        struct desk_win *w = &d->win[d->drag];

        w->x = x - d->dx;
        w->y = y - d->dy;
        if (w->y < 0) w->y = 0;
        if (w->y > bar - TITLE_H) w->y = bar - TITLE_H;
        if (w->x < -w->w + 40) w->x = -w->w + 40;
        if (w->x > d->scr.w - 40) w->x = d->scr.w - 40;
    } else if (d->painting && (btn & MOUSE_LEFT)) {
        struct desk_win *w = &d->win[WIN_PAINT];
        i32 cx = x - (w->x + 12), cy = y - (w->y + TITLE_H + 10);

        canvas_line(d, d->pmx, d->pmy, cx, cy);
        d->pmx = cx;
        d->pmy = cy;
    } else if (pressed) {
        if (y >= bar) {                                 /* barre des taches */
            i32 bx = 96;

            for (int i = 0; i < WIN_COUNT; i++) {
                i32 tw = gfx_text_width(d->win[i].title, 1) + 20;

                if (x >= bx && x < bx + tw) {
                    d->win[i].visible = !d->win[i].visible || d->order[WIN_COUNT - 1] != i;
                    if (d->win[i].visible)
                        raise_win(d, i);
                }
                bx += tw + 6;
            }
        } else {
            int id = desk_window_at(d, x, y);

            if (id >= 0) {
                struct desk_win *w = &d->win[id];

                raise_win(d, id);
                if (y < w->y + TITLE_H) {
                    if (x >= w->x + w->w - 22 && x < w->x + w->w - 10)
                        w->visible = 0;
                    else {
                        d->drag = id;
                        d->dx = x - w->x;
                        d->dy = y - w->y;
                    }
                } else if (id == WIN_PAINT) {
                    i32 cx = x - (w->x + 12), cy = y - (w->y + TITLE_H + 10);

                    if (cx >= 0 && cx < PAINT_W && cy >= 0 && cy < PAINT_H) {
                        d->painting = 1;
                        d->pmx = cx;
                        d->pmy = cy;
                        canvas_dot(d, cx, cy);
                    } else if (cy >= PAINT_H + 12 && cy < PAINT_H + 28 && cx >= 0 && cx / 32 < NINK && cx % 32 < 24)
                        d->ink = inks[cx / 32];
                }
            }
        }
    }
    if (right) {
        int id = desk_window_at(d, x, y);

        if (id == WIN_PAINT)
            for (int i = 0; i < PAINT_W * PAINT_H; i++)
                d->canvas[i] = C_PAPER;
    }
    d->prev_btn = btn;
}

void desk_key(struct desk *d, int key)
{
    if (key == 27)
        d->quit = 1;
}
