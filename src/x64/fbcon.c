/*
 * PhoenOS perf64 - x64/fbcon.c
 * Console graphique sur le framebuffer (voir fbcon.h). Ecrit seulement dans l'ecran, ne le relit jamais (la memoire
 * video est lente en lecture) : la grille de texte reste dans vga.c. Une cellule deja dessinee identique n'est pas
 * redessinee, ce qui rend le defilement d'un ecran presque vide quasi gratuit.
 */
#include "fbcon.h"
#include "bootinfo.h"

#define COLS 80
#define ROWS 25
#define CW   8
#define CH   16
#define MAX_SCALE 8
#ifdef FBCON_TEST
#define FBCON_ADDR_OK(a) 0                       /* tests : adresse d'un tableau quelconque */
#else
#define FBCON_ADDR_OK(a) ((a) >= (1ull << 32))   /* au-dela de 4 Gio : non mappe par boot.S */
#endif

static u8       *fb;                         /* adresse (identite sous 4 Gio) */
static u32       pitch, width, height, bpp_bytes;
static unsigned  scale;
static u32       org_x, org_y;               /* coin haut-gauche de la grille, en pixels */
static u32       pal_pix[16];
static u8        pal_rgb[16][3] = {
    {0x00,0x00,0x00},{0x00,0x00,0xAA},{0x00,0xAA,0x00},{0x00,0xAA,0xAA},
    {0xAA,0x00,0x00},{0xAA,0x00,0xAA},{0xAA,0x55,0x00},{0xAA,0xAA,0xAA},
    {0x55,0x55,0x55},{0x55,0x55,0xFF},{0x55,0xFF,0x55},{0x55,0xFF,0xFF},
    {0xFF,0x55,0x55},{0xFF,0x55,0xFF},{0xFF,0xFF,0x55},{0xFF,0xFF,0xFF}
};
static u32       drawn[ROWS][COLS];          /* derniere valeur dessinee (0xFFFFFFFF : inconnue) */
static usize     cur_y, cur_x;
static int       cur_on;
static int       ready;

static u32 scale_comp(u8 v, u8 size, u8 pos)
{
    if (size == 0)
        return 0;
    return (u32)(v >> (8 - (size > 8 ? 8 : size))) << pos;
}

static void pal_compute(unsigned c)
{
    pal_pix[c] = scale_comp(pal_rgb[c][0], boot_fb.red_size, boot_fb.red_pos) |
                 scale_comp(pal_rgb[c][1], boot_fb.green_size, boot_fb.green_pos) |
                 scale_comp(pal_rgb[c][2], boot_fb.blue_size, boot_fb.blue_pos);
}

static void put_pixels(u8 *dst, const u32 *src, u32 n)
{
    if (bpp_bytes == 4) {
        volatile u32 *d = (volatile u32 *)dst;

        for (u32 i = 0; i < n; i++)
            d[i] = src[i];
    } else {                                 /* 24 bits : 3 octets par pixel */
        for (u32 i = 0; i < n; i++) {
            dst[3 * i]     = (u8)src[i];
            dst[3 * i + 1] = (u8)(src[i] >> 8);
            dst[3 * i + 2] = (u8)(src[i] >> 16);
        }
    }
}

static void fill_rect(u32 x, u32 y, u32 w, u32 h, u32 pix)
{
    u32 line[256];

    for (u32 i = 0; i < 256; i++)
        line[i] = pix;
    for (u32 yy = y; yy < y + h && yy < height; yy++) {
        for (u32 xx = x; xx < x + w && xx < width; ) {
            u32 n = (x + w - xx < 256) ? x + w - xx : 256;

            if (xx + n > width)
                n = width - xx;
            put_pixels(fb + (usize)yy * pitch + (usize)xx * bpp_bytes, line, n);
            xx += n;
        }
    }
}

static void draw_cell(usize y, usize x)
{
    u32 value = drawn[y][x];
    u8  ch = (u8)value, attr = (u8)(value >> 8);
    u32 fgp = pal_pix[attr & 15], bgp = pal_pix[(attr >> 4) & 15];
    const u8 *glyph = (ch >= 0x20 && ch <= 0x7E) ? font8x16_ascii[ch - 0x20] : (ch ? font8x16_unknown : font8x16_ascii[0]);
    int cursor_here = cur_on && y == cur_y && x == cur_x;
    u32 px[CW * MAX_SCALE];
    u32 x0 = org_x + (u32)x * CW * scale, y0 = org_y + (u32)y * CH * scale;

    for (unsigned r = 0; r < CH; r++) {
        u8 bits = glyph[r];

        if (cursor_here && r >= CH - 2)
            bits = 0xFF;
        for (unsigned b = 0; b < CW; b++) {
            u32 p = (bits & (0x80 >> b)) ? fgp : bgp;

            for (unsigned s = 0; s < scale; s++)
                px[b * scale + s] = p;
        }
        for (unsigned s = 0; s < scale; s++) {
            u32 yy = y0 + r * scale + s;

            if (yy < height && x0 + CW * scale <= width)
                put_pixels(fb + (usize)yy * pitch + (usize)x0 * bpp_bytes, px, CW * scale);
        }
    }
}

void fbcon_cell(usize y, usize x, u16 value)
{
    if (!ready || y >= ROWS || x >= COLS)
        return;
    if (drawn[y][x] == (u32)value)
        return;
    drawn[y][x] = value;
    draw_cell(y, x);
}

void fbcon_cursor(usize y, usize x, int visible)
{
    usize oy = cur_y, ox = cur_x;
    int   was = cur_on;

    if (!ready)
        return;
    cur_y = y; cur_x = x; cur_on = visible && y < ROWS && x < COLS;
    if (was && oy < ROWS && ox < COLS && drawn[oy][ox] != 0xFFFFFFFFu && (oy != y || ox != x || !cur_on))
        draw_cell(oy, ox);
    if (cur_on && drawn[y][x] != 0xFFFFFFFFu)
        draw_cell(y, x);
}

void fbcon_palette(unsigned c, u8 r, u8 g, u8 b)
{
    c &= 15;
    pal_rgb[c][0] = r; pal_rgb[c][1] = g; pal_rgb[c][2] = b;
    if (!ready)
        return;
    pal_compute(c);
    for (usize y = 0; y < ROWS; y++)           /* les cellules deja dessinees changent d'apparence : tout est a refaire */
        for (usize x = 0; x < COLS; x++)
            if (drawn[y][x] != 0xFFFFFFFFu && (((drawn[y][x] >> 8) & 15) == c || ((drawn[y][x] >> 12) & 15) == c))
                draw_cell(y, x);
    fill_rect(0, 0, width, org_y, pal_pix[0]);  /* bordures : couleur 0 */
}

unsigned fbcon_scale(void)
{
    return ready ? scale : 0;
}

#ifndef FBCON_TEST
/* Memoire video en "ecriture combinee" (PAT) : les ecritures en rafale sont groupees, bien plus rapide que sans cache. */
extern u64 boot_pd[2048];

static void enable_write_combining(void)
{
    u32 lo, hi, eax, ebx, ecx, edx;
    u64 first = (u64)(usize)fb >> 21, last = ((u64)(usize)fb + (u64)pitch * height - 1) >> 21;

    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "0"(1), "2"(0));
    if (!(edx & (1u << 16)) || last >= 2048)    /* pas de PAT : on reste sans cache */
        return;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0x277));
    /* entree 4 (PAT=1, PCD=0, PWT=0) : WC (0x01). Les autres entrees gardent leur valeur par defaut. */
    __asm__ volatile("wrmsr" : : "c"(0x277), "a"(lo), "d"((hi & ~0xFFu) | 0x01u));
    for (u64 i = first; i <= last; i++)
        boot_pd[i] = (boot_pd[i] & ~0x18ull) | (1ull << 12);   /* PAT (bit 12) ; PCD et PWT a 0 */
    usize cr3;
    __asm__ volatile("mov %%cr3, %0; mov %0, %%cr3" : "=r"(cr3) : : "memory");
}
#else
static void enable_write_combining(void) {}
#endif

int fbcon_init(void)
{
    if (boot_fb.type != 1 || boot_fb.addr == 0 || FBCON_ADDR_OK(boot_fb.addr) ||
        (boot_fb.bpp != 32 && boot_fb.bpp != 24) || boot_fb.red_size == 0)
        return 0;
    fb = (u8 *)(usize)boot_fb.addr;
    pitch = boot_fb.pitch; width = boot_fb.width; height = boot_fb.height;
    bpp_bytes = boot_fb.bpp / 8u;
    scale = width / (COLS * CW);
    if (height / (ROWS * CH) < scale)
        scale = height / (ROWS * CH);
    if (scale == 0)
        return 0;                               /* ecran trop petit pour 80x25 */
    if (scale > MAX_SCALE)
        scale = MAX_SCALE;
    org_x = (width - COLS * CW * scale) / 2;
    org_y = (height - ROWS * CH * scale) / 2;
    enable_write_combining();
    for (unsigned c = 0; c < 16; c++)
        pal_compute(c);
    for (usize y = 0; y < ROWS; y++)
        for (usize x = 0; x < COLS; x++)
            drawn[y][x] = 0xFFFFFFFFu;
    ready = 1;
    cur_on = 0;
    fill_rect(0, 0, width, height, pal_pix[0]);
    return 1;
}
