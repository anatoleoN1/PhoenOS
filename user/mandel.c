/* mandel : ensemble de Mandelbrot en virgule fixe. Clic gauche : zoom sur le point ; clic droit : dezoom ; Echap : fin. */
#include "gfxlib.h"

#define W 256
#define H 160
#define FIX 24                                      /* virgule fixe 8.24 sur 32 bits, produits sur 64 bits */

static uint px[W * H];

static void render(int cx, int cy, int span)       /* centre (cx, cy), largeur `span` (virgule fixe) */
{
    int step = span / W;

    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int c_re = cx + (x - W / 2) * step, c_im = cy + (y - H / 2) * step;
            int zr = 0, zi = 0, i;

            for (i = 0; i < 64; i++) {
                long long r2 = ((long long)zr * zr) >> FIX, i2 = ((long long)zi * zi) >> FIX;

                if (r2 + i2 > (4LL << FIX))
                    break;
                zi = (int)((((long long)zr * zi) >> (FIX - 1)) + c_im);
                zr = (int)(r2 - i2 + c_re);
            }
            px[y * W + x] = i == 64 ? RGB(14, 18, 24) : RGB(40 + i * 3, 70 + i * 2, 120 + i * 2);
        }
}

int main(void)
{
    uint sc, ox, oy, prev = 0;
    int cx = -(1 << (FIX - 1)), cy = 0, span = 3 << FIX;
    struct mouse_info m;

    if (gfx_open(W, H, &sc, &ox, &oy) != 0)
        return 1;
    render(cx, cy, span);
    gfx_blit(px, W, H, W, ox, oy, sc);
    for (;;) {
        int k = inkey();

        if (k == 27)
            break;
        if (mouse_get(&m) == 0 && m.buttons != prev && m.buttons) {
            int mx = (m.x - (int)ox) / (int)sc, my = (m.y - (int)oy) / (int)sc;

            if (mx >= 0 && mx < W && my >= 0 && my < H) {
                int step = span / W;

                cx += (mx - W / 2) * step;
                cy += (my - H / 2) * step;
                span = (m.buttons & 1) ? span / 2 : (span < (6 << FIX) ? span * 2 : span);
                if (span < W * 16)
                    span = W * 16;                 /* limite de precision */
                render(cx, cy, span);
                gfx_blit(px, W, H, W, ox, oy, sc);
            }
        }
        prev = m.buttons;
        sleep_ms(20);
    }
    gfx_claim(0);
    return 0;
}
