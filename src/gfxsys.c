/*
 * PhoenOS - gfxsys.c
 * Ecran graphique et souris vus par les programmes : reservation, rectangles de pixels, etat de la souris.
 */
#include "gfxsys.h"
#include "mouse.h"

const struct gfx_dev *gfx_dev_hook;
static u32 owner;
static u32 rowbuf[2048];

err_t gfx_info(u32 *w, u32 *h)
{
    if (!gfx_dev_hook || !gfx_dev_hook->screen || !gfx_dev_hook->screen(w, h) || !*w || !*h)
        return E_NOENT;
    return E_OK;
}

err_t gfx_claim(u32 pid, int on)
{
    u32 w, h;

    if (gfx_info(&w, &h) != E_OK)
        return E_NOENT;
    if (on) {
        if (owner && owner != pid)
            return E_BUSY;
        if (!owner && gfx_dev_hook->claim)
            gfx_dev_hook->claim(1);
        owner = pid;
        return E_OK;
    }
    if (owner != pid)
        return E_INVAL;
    owner = 0;
    if (gfx_dev_hook->claim)
        gfx_dev_hook->claim(0);
    return E_OK;
}

u32 gfxsys_blit_span(const struct gfx_blit_req *r)
{
    if (!r->w || !r->h || r->w > 8192 || r->h > 8192 || r->stride < r->w || r->stride > 16384)
        return 0;
    if (!r->scale || r->scale > GFX_MAX_SCALE || (r->src & 3))
        return 0;
    return ((r->h - 1) * r->stride + r->w) * 4;
}

err_t gfxsys_blit(u32 pid, const struct gfx_blit_req *r)
{
    u32 sw, sh, s = r->scale, cw, ch;
    const u32 *src;

    if (!owner || owner != pid || !gfxsys_blit_span(r))
        return E_INVAL;
    if (gfx_info(&sw, &sh) != E_OK)
        return E_NOENT;
    if (r->x >= sw || r->y >= sh)
        return E_OK;
    cw = r->w * s;
    ch = r->h * s;
    if (cw > sw - r->x)
        cw = sw - r->x;                                 /* on rogne ce qui depasse de l'ecran */
    if (ch > sh - r->y)
        ch = sh - r->y;
    src = (const u32 *)(usize)r->src;
    if (s == 1) {
        gfx_dev_hook->present(src, r->stride, r->x, r->y, cw, ch);
        return E_OK;
    }
    /* Agrandissement : une ligne source devient s lignes, chaque pixel s pixels, par bandes de la taille du tampon. */
    for (u32 y = 0; y < ch; ) {
        u32 sy = y / s, n = s - y % s;
        const u32 *row = src + sy * r->stride;
        u32 xo = 0;

        if (n > ch - y)
            n = ch - y;
        while (xo < cw) {
            u32 take = cw - xo;

            if (take > 2048)
                take = 2048;
            for (u32 i = 0; i < take; i++)
                rowbuf[i] = row[(xo + i) / s];
            for (u32 k = 0; k < n; k++)
                gfx_dev_hook->present(rowbuf, take, r->x + xo, r->y + y + k, take, 1);
            xo += take;
        }
        y += n;
    }
    return E_OK;
}

err_t gfx_mouse(struct gfx_mouse *out)
{
    struct mouse_state m;

    if (!mouse_present())
        return E_NOENT;
    mouse_get(&m);
    out->x = m.x;
    out->y = m.y;
    out->buttons = m.buttons;
    out->wheel = m.wheel;
    return E_OK;
}

void gfx_proc_exit(u32 pid)
{
    if (owner && owner == pid)
        gfx_claim(pid, 0);
}

u32 gfx_owner(void)
{
    return owner;
}
