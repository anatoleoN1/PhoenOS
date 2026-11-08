/* dessin : petit programme de dessin a la souris. Clic gauche : tracer ; clic droit : effacer ; touches 1 a 6 : couleur ; Echap : fin. */
#include "gfxlib.h"

#define W 320
#define H 200

static uint px[W * H];
static const uint ink[6] = { RGB(34, 41, 48), RGB(194, 75, 58), RGB(219, 132, 46), RGB(63, 143, 107), RGB(47, 111, 176), RGB(122, 79, 158) };
#define PAPER RGB(235, 232, 225)

static void dot(int x, int y, uint c)
{
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++)
            if (x + i >= 0 && x + i < W && y + j >= 0 && y + j < H)
                px[(y + j) * W + x + i] = c;
}

int main(void)
{
    uint sc, ox, oy, c = ink[0];
    int lx = -1, ly = -1;
    struct mouse_info m;

    if (gfx_open(W, H, &sc, &ox, &oy) != 0)
        return 1;
    if (mouse_get(&m) != 0) {
        gfx_claim(0);
        puts("Pas de souris : cliquez d'abord dans la fenetre pour la capturer.\n");
        return 1;
    }
    for (int i = 0; i < W * H; i++)
        px[i] = PAPER;
    gfx_blit(px, W, H, W, ox, oy, sc);
    for (;;) {
        int k = inkey(), dirty = 0;

        if (k == 27)
            break;
        if (k >= '1' && k <= '6')
            c = ink[k - '1'];
        mouse_get(&m);
        int mx = (m.x - (int)ox) / (int)sc, my = (m.y - (int)oy) / (int)sc;

        if (m.buttons & 2) {
            for (int i = 0; i < W * H; i++)
                px[i] = PAPER;
            dirty = 1;
            lx = -1;
        } else if ((m.buttons & 1) && mx >= 0 && mx < W && my >= 0 && my < H) {
            int x0 = lx < 0 ? mx : lx, y0 = ly < 0 ? my : ly;
            int n = (mx > x0 ? mx - x0 : x0 - mx) > (my > y0 ? my - y0 : y0 - my) ? (mx > x0 ? mx - x0 : x0 - mx) : (my > y0 ? my - y0 : y0 - my);

            for (int s = 0; s <= n; s++)
                dot(n ? x0 + (mx - x0) * s / n : mx, n ? y0 + (my - y0) * s / n : my, c);
            lx = mx;
            ly = my;
            dirty = 1;
        } else
            lx = -1;
        if (dirty)
            gfx_blit(px, W, H, W, ox, oy, sc);
        sleep_ms(8);
    }
    gfx_claim(0);
    return 0;
}
