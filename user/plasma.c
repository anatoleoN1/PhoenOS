/* plasma : motif anime en couleurs (calcul entier uniquement). Echap ou n'importe quelle touche pour finir. */
#include "gfxlib.h"

#define W 256
#define H 160

static uint px[W * H];
static unsigned char wave[256];
static uint pal[256];

int main(void)
{
    uint sc, ox, oy, t = 0, start;

    if (gfx_open(W, H, &sc, &ox, &oy) != 0)
        return 1;
    for (int i = 0; i < 256; i++) {                 /* sinus approche par deux paraboles : 0..255 */
        int x = i < 128 ? i : 255 - i;               /* 0..127..0 */
        wave[i] = (unsigned char)(x * (255 - x) * 255 / (127 * 128) > 255 ? 255 : x * (255 - x) * 255 / (127 * 128));
        pal[i] = RGB(30 + wave[i] / 2, 40 + wave[(i + 85) & 255] / 3, 60 + wave[(i + 170) & 255] / 2);
    }
    start = uptime_ms();
    while (inkey() < 0 && uptime_ms() - start < 60000) {
        for (uint y = 0; y < H; y++)
            for (uint x = 0; x < W; x++) {
                uint v = wave[(x * 2 + t) & 255] + wave[(y * 3 + t * 2) & 255] + wave[((x + y) + t * 3) & 255];

                px[y * W + x] = pal[(v / 3 + t) & 255];
            }
        if (gfx_blit(px, W, H, W, ox, oy, sc) != 0)
            break;
        t += 2;
        sleep_ms(16);
    }
    gfx_claim(0);
    return 0;
}
