/* Ecran graphique vu par les programmes (gfxsys.c) avec un faux ecran : reservation, rognage, agrandissement, souris. */
#include "testlib.h"
#include "gfxsys.h"
#include "mouse.h"
u8 test_scancode;

#define SW 40
#define SH 30
static u32 screen[SW * SH];
static int claimed, claim_calls, present_calls;

static int  f_screen(u32 *w, u32 *h) { *w = SW; *h = SH; return 1; }
static void f_present(const u32 *src, u32 stride, u32 x, u32 y, u32 w, u32 h)
{
    present_calls++;
    for (u32 j = 0; j < h; j++)
        for (u32 i = 0; i < w; i++) {
            if (x + i >= SW || y + j >= SH) { screen[0] = 0xDEAD; return; }
            screen[(y + j) * SW + x + i] = src[j * stride + i];
        }
}
static void f_claim(int on) { claimed = on; claim_calls++; }
static const struct gfx_dev fake = { .screen = f_screen, .present = f_present, .claim = f_claim };

static u32 src[16 * 16] __attribute__((aligned(4)));

int test_main(void)
{
    u32 w, h;
    struct gfx_blit_req r = { 0, 0, 4, 3, (u32)(usize)src, 16, 1 };
    struct gfx_mouse gm;

    gfx_dev_hook = NULL;
    CHECK(gfx_info(&w, &h) == E_NOENT);
    CHECK(gfx_claim(7, 1) == E_NOENT);
    gfx_dev_hook = &fake;
    CHECK(gfx_info(&w, &h) == E_OK && w == SW && h == SH);

    CHECK(gfxsys_blit(7, &r) == E_INVAL);                       /* pas reserve */
    CHECK(gfx_claim(7, 1) == E_OK && claimed == 1 && gfx_owner() == 7);
    CHECK(gfx_claim(7, 1) == E_OK && claim_calls == 1);          /* deja a lui : rien de plus */
    CHECK(gfx_claim(8, 1) == E_BUSY);                           /* un autre programme n'a pas le droit */
    CHECK(gfx_claim(8, 0) == E_INVAL);
    CHECK(gfxsys_blit(8, &r) == E_INVAL);

    for (int i = 0; i < 16 * 16; i++)
        src[i] = 0x100 + (u32)i;
    CHECK(gfxsys_blit(7, &r) == E_OK);                          /* echelle 1 */
    CHECK(screen[0] == 0x100 && screen[3] == 0x103 && screen[2 * SW + 3] == 0x100 + 2 * 16 + 3 && screen[4] == 0);

    r.scale = 3; r.x = 10; r.y = 5; r.w = 2; r.h = 2;           /* chaque pixel devient 3 x 3 */
    memset(screen, 0, sizeof screen);
    CHECK(gfxsys_blit(7, &r) == E_OK);
    CHECK(screen[5 * SW + 10] == 0x100 && screen[7 * SW + 12] == 0x100 && screen[5 * SW + 13] == 0x101 &&
          screen[8 * SW + 10] == 0x110 && screen[10 * SW + 15] == 0x111 && screen[5 * SW + 16] == 0 && screen[11 * SW + 10] == 0);

    r.x = 38; r.y = 28; r.w = 4; r.h = 4; r.scale = 2;          /* depasse de l'ecran : rogne, jamais d'ecriture hors ecran */
    memset(screen, 0, sizeof screen);
    CHECK(gfxsys_blit(7, &r) == E_OK && screen[0] != 0xDEAD);
    CHECK(screen[28 * SW + 38] == 0x100 && screen[29 * SW + 39] == 0x100);
    r.x = 100;                                                   /* entierement hors ecran */
    CHECK(gfxsys_blit(7, &r) == E_OK);

    /* requetes invalides */
    r = (struct gfx_blit_req){ 0, 0, 0, 3, (u32)(usize)src, 16, 1 };   CHECK(gfxsys_blit_span(&r) == 0);
    r = (struct gfx_blit_req){ 0, 0, 4, 3, (u32)(usize)src, 3, 1 };    CHECK(gfxsys_blit_span(&r) == 0);    /* stride < largeur */
    r = (struct gfx_blit_req){ 0, 0, 4, 3, (u32)(usize)src, 16, 0 };   CHECK(gfxsys_blit_span(&r) == 0);
    r = (struct gfx_blit_req){ 0, 0, 4, 3, (u32)(usize)src, 16, 9 };   CHECK(gfxsys_blit_span(&r) == 0);
    r = (struct gfx_blit_req){ 0, 0, 4, 3, (u32)(usize)src + 1, 16, 1 }; CHECK(gfxsys_blit_span(&r) == 0);  /* non aligne */
    r = (struct gfx_blit_req){ 0, 0, 4, 3, (u32)(usize)src, 16, 1 };
    CHECK(gfxsys_blit_span(&r) == ((3 - 1) * 16 + 4) * 4);       /* octets lus : derniere ligne comprise, sans le reste de sa ligne */
    r.w = 100000;                                                CHECK(gfxsys_blit_span(&r) == 0);

    /* souris */
    mouse_set_present(0);
    CHECK(gfx_mouse(&gm) == E_NOENT);
    mouse_set_present(1);
    mouse_set_bounds(SW, SH);
    CHECK(gfx_mouse(&gm) == E_OK && gm.x == SW / 2 && gm.y == SH / 2 && gm.buttons == 0);

    /* fin du programme : l'ecran est rendu a la console, mais seulement par son proprietaire */
    gfx_proc_exit(8);
    CHECK(gfx_owner() == 7 && claimed == 1);
    gfx_proc_exit(7);
    CHECK(gfx_owner() == 0 && claimed == 0);
    CHECK(gfx_claim(8, 1) == E_OK);
    CHECK(gfx_claim(8, 0) == E_OK && gfx_owner() == 0);
    out("  ecran des programmes : reservation, rognage, agrandissement, souris, nettoyage a la fin\n");
    return 0;
}
