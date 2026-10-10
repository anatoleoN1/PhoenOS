/*
 * PhoenOS - vga.c
 * Pilote texte VGA 80x25 (mémoire vidéo en 0xB8000).
 */
#include "vga.h"

#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_MEMORY  ((volatile u16 *)0xB8000)

static usize row, col;
static u8 color;

static inline u16 entry(char c, u8 attr)
{
    return (u16)(u8)c | ((u16)attr << 8);
}

void vga_set_color(enum vga_color fg, enum vga_color bg)
{
    color = (u8)(fg | (bg << 4));
}

void vga_clear(void)
{
    for (usize i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_MEMORY[i] = entry(' ', color);
    row = col = 0;
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

void vga_putc(char c)
{
    if (c == '\n') {
        col = 0;
        if (++row == VGA_HEIGHT)
            scroll();
        return;
    }

    VGA_MEMORY[row * VGA_WIDTH + col] = entry(c, color);

    if (++col == VGA_WIDTH) {
        col = 0;
        if (++row == VGA_HEIGHT)
            scroll();
    }
}

void vga_puts(const char *s)
{
    while (*s)
        vga_putc(*s++);
}
