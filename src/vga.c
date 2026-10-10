/*
 * PhoenOS - vga.c
 * Pilote texte VGA 80x25 (mémoire vidéo en 0xB8000) avec curseur matériel.
 */
#include "vga.h"
#include "io.h"

#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_MEMORY  ((volatile u16 *)0xB8000)

#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA  0x3D5

static usize row, col;
static u8 color;

static inline u16 entry(char c, u8 attr)
{
    return (u16)(u8)c | ((u16)attr << 8);
}

static void cursor_update(void)
{
    u16 pos = (u16)(row * VGA_WIDTH + col);

    outb(VGA_CRTC_INDEX, 14);
    outb(VGA_CRTC_DATA, (u8)(pos >> 8));
    outb(VGA_CRTC_INDEX, 15);
    outb(VGA_CRTC_DATA, (u8)(pos & 0xFF));
}

void vga_set_color(enum vga_color fg, enum vga_color bg)
{
    color = (u8)(fg | (bg << 4));
}

void vga_set_palette(enum vga_color c, u8 r, u8 g, u8 b)
{
    /* Index du DAC utilisé par chacune des 16 couleurs du mode texte standard */
    static const u8 dac_index[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
        0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F
    };

    outb(0x3C8, dac_index[c & 15]);
    outb(0x3C9, (u8)(r >> 2));          /* le DAC travaille sur 6 bits par composante */
    outb(0x3C9, (u8)(g >> 2));
    outb(0x3C9, (u8)(b >> 2));
}

void vga_clear(void)
{
    for (usize i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_MEMORY[i] = entry(' ', color);
    row = col = 0;
    cursor_update();
}

void vga_init(void)
{
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
}

static void scroll(void)
{
    for (usize y = 1; y < VGA_HEIGHT; y++)
        for (usize x = 0; x < VGA_WIDTH; x++)
            VGA_MEMORY[(y - 1) * VGA_WIDTH + x] = VGA_MEMORY[y * VGA_WIDTH + x];

    for (usize x = 0; x < VGA_WIDTH; x++)
        VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = entry(' ', color);

    row = VGA_HEIGHT - 1;
}

static void newline(void)
{
    col = 0;
    if (++row == VGA_HEIGHT)
        scroll();
}

void vga_putc(char c)
{
    switch (c) {
    case '\n':
        newline();
        break;
    case '\r':
        col = 0;
        break;
    case '\b':
        if (col > 0) {
            col--;
        } else if (row > 0) {
            row--;
            col = VGA_WIDTH - 1;
        }
        VGA_MEMORY[row * VGA_WIDTH + col] = entry(' ', color);
        break;
    case '\t':
        do {
            VGA_MEMORY[row * VGA_WIDTH + col] = entry(' ', color);
            if (++col == VGA_WIDTH) {
                newline();
                break;
            }
        } while (col % 4);
        break;
    default:
        VGA_MEMORY[row * VGA_WIDTH + col] = entry(c, color);
        if (++col == VGA_WIDTH)
            newline();
        break;
    }

    cursor_update();
}

void vga_puts(const char *s)
{
    while (*s)
        vga_putc(*s++);
}
