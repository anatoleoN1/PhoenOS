/*
 * PhoenOS perf64 - x64/gfx.c : primitives de dessin sur surface en memoire (voir gfx.h).
 */
#include "gfx.h"
#include "fbcon.h"

void gfx_surface_init(struct gfx_surface *s, u32 *px, i32 w, i32 h, i32 stride)
{
    s->px = px; s->w = w; s->h = h; s->stride = stride;
    s->cx0 = 0; s->cy0 = 0; s->cx1 = w; s->cy1 = h;
}

void gfx_reset_clip(struct gfx_surface *s)
{
    s->cx0 = 0; s->cy0 = 0; s->cx1 = s->w; s->cy1 = s->h;
}

void gfx_set_clip(struct gfx_surface *s, i32 x, i32 y, i32 w, i32 h)
{
    i32 x1 = x + w, y1 = y + h;

    s->cx0 = x < 0 ? 0 : x;
    s->cy0 = y < 0 ? 0 : y;
    s->cx1 = x1 > s->w ? s->w : x1;
    s->cy1 = y1 > s->h ? s->h : y1;
    if (s->cx1 < s->cx0) s->cx1 = s->cx0;
    if (s->cy1 < s->cy0) s->cy1 = s->cy0;
}

/* Rogne [x, x+w) x [y, y+h) a la decoupe ; 0 si rien ne reste. */
static int clip_rect(const struct gfx_surface *s, i32 *x, i32 *y, i32 *w, i32 *h)
{
    i64 x0 = *x, y0 = *y, x1 = (i64)*x + *w, y1 = (i64)*y + *h;

    if (*w <= 0 || *h <= 0)
        return 0;
    if (x0 < s->cx0) x0 = s->cx0;
    if (y0 < s->cy0) y0 = s->cy0;
    if (x1 > s->cx1) x1 = s->cx1;
    if (y1 > s->cy1) y1 = s->cy1;
    if (x1 <= x0 || y1 <= y0)
        return 0;
    *x = (i32)x0; *y = (i32)y0; *w = (i32)(x1 - x0); *h = (i32)(y1 - y0);
    return 1;
}

void gfx_fill(struct gfx_surface *s, i32 x, i32 y, i32 w, i32 h, u32 rgb)
{
    if (!clip_rect(s, &x, &y, &w, &h))
        return;
    for (i32 j = 0; j < h; j++) {
        u32 *row = s->px + (usize)(y + j) * (usize)s->stride + (usize)x;

        for (i32 i = 0; i < w; i++)
            row[i] = rgb;
    }
}

void gfx_hline(struct gfx_surface *s, i32 x, i32 y, i32 w, u32 rgb)
{
    gfx_fill(s, x, y, w, 1, rgb);
}

void gfx_vline(struct gfx_surface *s, i32 x, i32 y, i32 h, u32 rgb)
{
    gfx_fill(s, x, y, 1, h, rgb);
}

void gfx_pixel(struct gfx_surface *s, i32 x, i32 y, u32 rgb)
{
    if (x >= s->cx0 && x < s->cx1 && y >= s->cy0 && y < s->cy1)
        s->px[(usize)y * (usize)s->stride + (usize)x] = rgb;
}

u32 gfx_mix(u32 a, u32 b, unsigned alpha)
{
    u32 r = 0;

    if (alpha > 255)
        alpha = 255;
    for (int sh = 0; sh <= 16; sh += 8) {
        u32 ca = (a >> sh) & 0xFF, cb = (b >> sh) & 0xFF;

        r |= ((ca * (255 - alpha) + cb * alpha + 127) / 255) << sh;
    }
    return r;
}

void gfx_gradient_v(struct gfx_surface *s, i32 x, i32 y, i32 w, i32 h, u32 top, u32 bottom)
{
    i32 y0 = y, h0 = h;

    for (i32 j = 0; j < h0; j++) {
        u32 c = gfx_mix(top, bottom, h0 > 1 ? (unsigned)((u32)j * 255u / (u32)(h0 - 1)) : 0);

        gfx_fill(s, x, y0 + j, w, 1, c);
    }
}

void gfx_blend(struct gfx_surface *s, i32 x, i32 y, i32 w, i32 h, u32 rgb, unsigned alpha)
{
    if (alpha == 0 || !clip_rect(s, &x, &y, &w, &h))
        return;
    if (alpha > 255)
        alpha = 255;
    for (i32 j = 0; j < h; j++) {
        u32 *row = s->px + (usize)(y + j) * (usize)s->stride + (usize)x;

        for (i32 i = 0; i < w; i++)
            row[i] = gfx_mix(row[i], rgb, alpha);
    }
}

i32 gfx_text_width(const char *str, unsigned scale)
{
    i32 n = 0;

    while (*str++)
        n++;
    return n * 8 * (i32)(scale ? scale : 1);
}

i32 gfx_text(struct gfx_surface *s, i32 x, i32 y, const char *str, u32 fg, i64 bg, unsigned scale)
{
    i32 x0 = x;

    if (scale == 0)
        scale = 1;
    for (; *str; str++, x += 8 * (i32)scale) {
        u8 ch = (u8)*str;
        const u8 *g = (ch >= 0x20 && ch <= 0x7E) ? font8x16_ascii[ch - 0x20] : (ch ? font8x16_unknown : font8x16_ascii[0]);

        if (x >= s->cx1 || x + 8 * (i32)scale <= s->cx0 || y >= s->cy1 || y + 16 * (i32)scale <= s->cy0)
            continue;                           /* entierement hors decoupe */
        for (unsigned r = 0; r < 16; r++)
            for (unsigned b = 0; b < 8; b++) {
                int on = (g[r] & (0x80 >> b)) != 0;

                if (!on && bg < 0)
                    continue;
                gfx_fill(s, x + (i32)(b * scale), y + (i32)(r * scale), (i32)scale, (i32)scale, on ? fg : (u32)bg);
            }
    }
    return x - x0;
}

void gfx_blit(struct gfx_surface *dst, i32 x, i32 y, const u32 *src, i32 sw, i32 sh, i32 sstride, unsigned scale)
{
    if (scale == 0)
        scale = 1;
    for (i32 j = 0; j < sh; j++) {
        i32 dy = y + j * (i32)scale;

        if (dy >= dst->cy1 || dy + (i32)scale <= dst->cy0)
            continue;
        for (i32 i = 0; i < sw; i++) {
            i32 dx = x + i * (i32)scale;

            if (dx >= dst->cx1 || dx + (i32)scale <= dst->cx0)
                continue;
            if (scale == 1)
                gfx_pixel(dst, dx, dy, src[(usize)j * (usize)sstride + (usize)i]);
            else
                gfx_fill(dst, dx, dy, (i32)scale, (i32)scale, src[(usize)j * (usize)sstride + (usize)i]);
        }
    }
}
