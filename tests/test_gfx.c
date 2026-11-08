/* Dessin 2D (x64/gfx.c) et logique du bureau (x64/desktop.c) sur une surface en memoire. */
#include "testlib.h"
#include "gfx.h"
#include "desktop.h"
#include "mouse.h"
u8 test_scancode;
void vga_putc(char c) { (void)c; }
void serial_putc(char c) { (void)c; }

#define W 640
#define H 400
static u32 px[W * H], canvas[PAINT_W * PAINT_H];
static struct desk d;

int test_main(void)
{
    struct gfx_surface s;

    gfx_surface_init(&s, px, W, H, W);
    memset(px, 0, sizeof px);
    gfx_fill(&s, 10, 10, 20, 5, 0xFF0000);
    CHECK(px[10 * W + 10] == 0xFF0000 && px[14 * W + 29] == 0xFF0000 && px[15 * W + 10] == 0 && px[10 * W + 30] == 0 && px[9 * W + 10] == 0);

    /* rognage : aucun debordement, y compris avec des coordonnees enormes ou negatives */
    gfx_fill(&s, -5, -5, 10, 10, 0x00FF00);
    CHECK(px[0] == 0x00FF00 && px[4 * W + 4] == 0x00FF00 && px[5 * W + 5] == 0);
    gfx_fill(&s, W - 3, H - 3, 100, 100, 0x0000FF);
    CHECK(px[(H - 1) * W + W - 1] == 0x0000FF && px[(H - 3) * W + W - 3] == 0x0000FF);
    gfx_fill(&s, 0x7FFFFFF0, 0, 100, 10, 1);
    gfx_fill(&s, 0, 0x7FFFFFF0, 100, 100, 1);
    gfx_fill(&s, -0x7FFFFFF0, -0x7FFFFFF0, 100, 100, 1);
    gfx_pixel(&s, -1, 0, 5); gfx_pixel(&s, W, 0, 5); gfx_pixel(&s, 0, H, 5);

    gfx_set_clip(&s, 100, 100, 10, 10);
    gfx_fill(&s, 90, 90, 40, 40, 0xABCDEF);
    CHECK(px[100 * W + 100] == 0xABCDEF && px[109 * W + 109] == 0xABCDEF && px[110 * W + 100] == 0 && px[99 * W + 100] == 0 && px[100 * W + 110] == 0);
    gfx_reset_clip(&s);

    gfx_hline(&s, 200, 50, 5, 0x123456); gfx_vline(&s, 200, 50, 5, 0x654321);
    CHECK(px[50 * W + 204] == 0x123456 && px[54 * W + 200] == 0x654321 && px[55 * W + 200] == 0);

    CHECK(gfx_mix(0x000000, 0xFFFFFF, 0) == 0 && gfx_mix(0x000000, 0xFFFFFF, 255) == 0xFFFFFF);
    CHECK(gfx_mix(0xFF0000, 0x0000FF, 128) == 0x80007F || gfx_mix(0xFF0000, 0x0000FF, 128) == 0x7F0080 || gfx_mix(0xFF0000, 0x0000FF, 128) == 0x80007F);
    gfx_fill(&s, 300, 300, 4, 4, 0x808080);
    gfx_blend(&s, 300, 300, 4, 4, 0xFFFFFF, 0);
    CHECK(px[300 * W + 300] == 0x808080);
    gfx_blend(&s, 300, 300, 4, 4, 0xFFFFFF, 255);
    CHECK(px[300 * W + 300] == 0xFFFFFF);

    gfx_gradient_v(&s, 400, 100, 4, 11, 0x000000, 0xFF0000);
    CHECK(px[100 * W + 400] == 0 && px[110 * W + 400] == 0xFF0000 && (px[105 * W + 400] >> 16) > 100 && (px[105 * W + 400] >> 16) < 160);

    CHECK(gfx_text_width("abc", 1) == 24 && gfx_text_width("abc", 3) == 72);
    memset(px, 0, sizeof px);
    CHECK(gfx_text(&s, 10, 10, "H", 0xFFFFFF, 0x102030, 1) == 8);
    {
        int fg = 0, bg = 0;

        for (int y = 10; y < 26; y++)
            for (int x = 10; x < 18; x++) {
                fg += px[y * W + x] == 0xFFFFFF;
                bg += px[y * W + x] == 0x102030;
            }
        CHECK(fg > 10 && bg > 10 && fg + bg == 8 * 16);
    }
    gfx_text(&s, W - 4, H - 4, "WWWW", 0xFFFFFF, -1, 4);         /* texte qui sort de la surface : pas d'ecriture hors tableau */

    {   /* copie avec agrandissement */
        static u32 small[4] = { 1, 2, 3, 4 };

        memset(px, 0, sizeof px);
        gfx_blit(&s, 20, 20, small, 2, 2, 2, 3);
        CHECK(px[20 * W + 20] == 1 && px[22 * W + 22] == 1 && px[20 * W + 23] == 2 && px[23 * W + 20] == 3 && px[25 * W + 25] == 4 && px[26 * W + 26] == 0);
    }

    /* ---- bureau ---- */
    desk_init(&d, px, W, H, canvas);
    CHECK(d.win[WIN_CLOCK].visible && d.order[WIN_COUNT - 1] == WIN_HELP);
    for (int i = 0; i < WIN_COUNT; i++)                                  /* les fenetres restent sur l'ecran */
        CHECK(d.win[i].x >= 0 && d.win[i].y >= 0 && d.win[i].x + d.win[i].w <= W && d.win[i].y + d.win[i].h <= H - 32);
    desk_draw(&d);
    {
        int diff = 0;

        for (int i = 1; i < W * (H - 32); i++)
            diff += px[i] != px[0];
        CHECK(diff > 1000);
    }

    {   /* clic sur une fenetre : elle passe devant ; glisser sa barre de titre la deplace */
        struct desk_win *w = &d.win[WIN_CLOCK];
        i32 ox = w->x, oy = w->y;

        CHECK(desk_window_at(&d, w->x + 3, w->y + 3) == WIN_CLOCK);
        desk_mouse(&d, w->x + 30, w->y + 5, MOUSE_LEFT);
        CHECK(d.order[WIN_COUNT - 1] == WIN_CLOCK && d.drag == WIN_CLOCK);
        desk_mouse(&d, w->x + 30 + 17, w->y + 5 + 9, MOUSE_LEFT);
        CHECK(w->x == ox + 17 && w->y == oy + 9);
        desk_mouse(&d, 0, 0, 0);
        CHECK(d.drag == -1);
        desk_mouse(&d, 5000, 5000, MOUSE_LEFT);
        desk_mouse(&d, 5000, 5000, 0);
    }
    {   /* deplacement extreme : la barre de titre reste atteignable */
        struct desk_win *w = &d.win[WIN_SYSTEM];

        desk_mouse(&d, w->x + 20, w->y + 5, MOUSE_LEFT);
        desk_mouse(&d, -4000, -4000, MOUSE_LEFT);
        CHECK(w->x > -w->w && w->y >= 0);
        desk_mouse(&d, 9000, 9000, MOUSE_LEFT);
        CHECK(w->x < W && w->y < H - 32);
        desk_mouse(&d, 0, 0, 0);
    }
    {   /* fermer, puis rouvrir par la barre des taches */
        struct desk_win *w = &d.win[WIN_HELP];
        i32 bx = 96;

        desk_mouse(&d, 0, 0, 0);
        desk_mouse(&d, w->x + w->w - 16, w->y + 10, MOUSE_LEFT);
        desk_mouse(&d, 0, 0, 0);
        CHECK(!w->visible);
        for (int i = 0; i < WIN_HELP; i++)
            bx += gfx_text_width(d.win[i].title, 1) + 20 + 6;
        desk_mouse(&d, bx + 4, H - 10, MOUSE_LEFT);
        desk_mouse(&d, 0, 0, 0);
        CHECK(w->visible && d.order[WIN_COUNT - 1] == WIN_HELP);
    }
    {   /* dessin : un trait tracé dans la fenetre Dessin, clic droit pour effacer */
        struct desk_win *w = &d.win[WIN_PAINT];
        i32 cx = w->x + 12, cy = w->y + 24 + 10, n = 0;

        w->x = 100; w->y = 60;
        cx = w->x + 12; cy = w->y + 24 + 10;
        for (int i = 0; i < WIN_COUNT; i++)                              /* aucune autre fenetre au-dessus */
            if (i != WIN_PAINT)
                d.win[i].visible = 0;
        desk_mouse(&d, cx + 10, cy + 10, MOUSE_LEFT);
        desk_mouse(&d, cx + 50, cy + 10, MOUSE_LEFT);
        desk_mouse(&d, 0, 0, 0);
        for (int i = 0; i < PAINT_W * PAINT_H; i++)
            n += canvas[i] != canvas[PAINT_W * PAINT_H - 1];
        CHECK(n >= 40 * 3);
        CHECK(canvas[10 * PAINT_W + 30] != canvas[PAINT_W * PAINT_H - 1]);
        desk_mouse(&d, cx + 20, cy + 20, MOUSE_RIGHT);
        desk_mouse(&d, 0, 0, 0);
        n = 0;
        for (int i = 0; i < PAINT_W * PAINT_H; i++)
            n += canvas[i] != canvas[PAINT_W * PAINT_H - 1];
        CHECK(n == 0);
        /* un trait qui sort de la zone ne depasse pas le tableau */
        desk_mouse(&d, cx + 10, cy + 10, MOUSE_LEFT);
        desk_mouse(&d, cx + 4000, cy + 4000, MOUSE_LEFT);
        desk_mouse(&d, 0, 0, 0);
        /* choix d'une couleur */
        desk_mouse(&d, cx + 2 * 32 + 3, cy + PAINT_H + 14, MOUSE_LEFT);
        desk_mouse(&d, 0, 0, 0);
        CHECK(d.ink != canvas[PAINT_W * PAINT_H - 1] && d.ink != 0);
    }
    desk_key(&d, 'a');
    CHECK(!d.quit);
    desk_key(&d, 27);
    CHECK(d.quit);
    out("  dessin 2D et bureau : rognage, texte, melange, fenetres deplacables, fermeture, dessin\n");
    return 0;
}
