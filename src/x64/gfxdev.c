/*
 * PhoenOS perf64 - x64/gfxdev.c : le materiel graphique de l'edition 64 bits, branche sur gfxsys.
 */
#include "gfxdev.h"
#include "gfxsys.h"
#include "bochs.h"
#include "desktop.h"
#include "fbcon.h"
#include "mouse.h"
#include "vga.h"
#include "dlog.h"

static void dev_claim(int on)
{
    if (on) {
        fbcon_release();
    } else {
        fbcon_reclaim();
        vga_refresh();
    }
}

static err_t dev_set_mode(u32 w, u32 h)
{
    return bochs_set_mode(w, h);
}

static int dev_modes(struct gfx_mode *out, int max)
{
    struct bochs_mode m[16];
    int n = bochs_modes(m, 16);

    if (n > max)
        n = max;
    for (int i = 0; i < n; i++) {
        out[i].w = m[i].w;
        out[i].h = m[i].h;
    }
    return n;
}

static const struct gfx_dev dev = {
    .screen = fbcon_screen,
    .present = fbcon_present,
    .claim = dev_claim,
    .set_mode = dev_set_mode,
    .modes = dev_modes,
    .desktop = desktop_run,
};

void gfx_x64_init(void)
{
    u32 w, h;

    if (!fbcon_screen(&w, &h))
        return;                                         /* texte VGA : pas de graphique */
    gfx_dev_hook = &dev;
    if (mouse_init() == E_OK)
        mouse_set_bounds((i32)w, (i32)h);
}
