/* Console graphique 64 bits (x64/fbcon.c) avec un faux framebuffer : echelle, centrage, glyphes, curseur, palette. */
#include "testlib.h"
#include "fbcon.h"
#include "bootinfo.h"

u8 test_scancode;
struct boot_fb boot_fb;
static u32 fb[1280 * 800];
#define PX(x, y) fb[(y) * 1280 + (x)]

static void setfb(u32 w, u32 h)
{
    memset(&boot_fb, 0, sizeof boot_fb);
    boot_fb.addr = (u64)(usize)fb; boot_fb.pitch = w * 4; boot_fb.width = w; boot_fb.height = h;
    boot_fb.bpp = 32; boot_fb.type = 1;
    boot_fb.red_pos = 16; boot_fb.red_size = 8; boot_fb.green_pos = 8; boot_fb.green_size = 8;
    boot_fb.blue_pos = 0; boot_fb.blue_size = 8;
}

int test_main(void)
{
    memset(&boot_fb, 0, sizeof boot_fb);
    CHECK(fbcon_init() == 0);                           /* pas d'ecran graphique */
    setfb(600, 400);
    CHECK(fbcon_init() == 0);                           /* trop petit pour 80x25 */

    setfb(1280, 800);
    memset(fb, 0x55, sizeof fb);
    CHECK(fbcon_init() == 1);
    CHECK(fbcon_scale() == 2);
    CHECK(PX(0, 0) == 0 && PX(1279, 799) == 0);         /* ecran efface (noir) */

    /* 'A' blanc (attribut 0x0F) en cellule (0,0) : ligne 4 de la police = 0x18 -> pixels 3 et 4 -> x = 6..9, y = 8..9 */
    fbcon_cell(0, 0, (u16)('A' | (0x0F << 8)));
    CHECK(PX(6, 8) == 0xFFFFFF && PX(9, 9) == 0xFFFFFF);
    CHECK(PX(5, 8) == 0 && PX(10, 8) == 0 && PX(6, 7) == 0);

    /* fond bleu (1), texte jaune (14), cellule (2,3) = pixels 48.., 64.. */
    fbcon_cell(2, 3, (u16)(' ' | (0x1E << 8)));
    CHECK(PX(3 * 16, 2 * 32) == 0x0000AA && PX(3 * 16 + 15, 2 * 32 + 31) == 0x0000AA);

    /* curseur : 2 dernieres lignes de police (4 pixels) en couleur de texte, puis effacement quand il bouge */
    fbcon_cursor(0, 0, 1);
    CHECK(PX(0, 31) == 0xFFFFFF && PX(15, 28) == 0xFFFFFF && PX(0, 27) == 0);
    fbcon_cell(1, 1, (u16)(' ' | (0x07 << 8)));
    fbcon_cursor(1, 1, 1);
    CHECK(PX(0, 31) == 0);                              /* ancien emplacement redessine sans curseur */
    CHECK(PX(16, 63) == 0xAAAAAA);                      /* curseur dans la cellule (1,1) */
    fbcon_cursor(1, 1, 0);

    /* palette : la couleur 4 (rouge) devient verte : les cellules deja dessinees avec elle sont redessinees */
    fbcon_cell(5, 5, (u16)(' ' | (0x40 << 8)));
    CHECK(PX(5 * 16, 5 * 32) == 0xAA0000);
    fbcon_palette(4, 0x10, 0x20, 0x30);
    CHECK(PX(5 * 16, 5 * 32) == 0x102030);

    /* ecran plus grand : centre ; 1920x1080 -> echelle 2, marge (1920-1280)/2 = 320 en x, (1080-800)/2 = 140 en y */
    {
        static u32 big[1920 * 1080];

        setfb(1920, 1080);
        boot_fb.addr = (u64)(usize)big;
        CHECK(fbcon_init() == 1 && fbcon_scale() == 2);
        fbcon_cell(0, 0, (u16)('A' | (0x0F << 8)));
        CHECK(big[(140 + 8) * 1920 + 320 + 6] == 0xFFFFFF);
        CHECK(big[(140 + 8) * 1920 + 319] == 0);
    }
    return 0;
}
