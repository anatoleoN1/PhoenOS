/*
 * PhoenOS - vga.c
 * Pilote texte VGA 80x25 (mémoire vidéo en 0xB8000) avec curseur matériel.
 */
#include "vga.h"
#include "io.h"

#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#ifdef VGA_TEST_MEMORY                          /* les tests fournissent une fausse mémoire vidéo */
extern volatile u16 VGA_TEST_MEMORY[VGA_WIDTH * VGA_HEIGHT];
#define VGA_MEMORY  VGA_TEST_MEMORY
#else
#define VGA_MEMORY  ((volatile u16 *)0xB8000)
#endif

#define VGA_CRTC_INDEX 0x3D4
#define VGA_CRTC_DATA  0x3D5

#define SCROLLBACK_LINES 500                    /* lignes conservées au-dessus de l'écran */

/*
 * L'écran « vivant » est d'abord dessiné dans `live` (copie en RAM), puis recopié dans la
 * mémoire vidéo. Les lignes qui sortent par le haut vont dans `ring` (tampon circulaire).
 * Pour remonter dans l'historique, on affiche une fenêtre sur ring + live ; toute écriture
 * ramène automatiquement en bas.
 */
static u16 live[VGA_HEIGHT][VGA_WIDTH];
static u16 ring[SCROLLBACK_LINES][VGA_WIDTH];
static usize ring_count;                        /* lignes valides dans le tampon (<= SCROLLBACK_LINES) */
static usize ring_head;                         /* indice où écrire la prochaine ligne */
static usize view_offset;                       /* 0 = en bas ; sinon nombre de lignes remontées */

static usize row, col;
static u8 color;

static inline u16 entry(char c, u8 attr)
{
    return (u16)(u8)c | ((u16)attr << 8);
}

static void cursor_update(void)
{
    u16 pos = (u16)(row * VGA_WIDTH + col);

    if (view_offset)
        pos = VGA_WIDTH * VGA_HEIGHT;           /* hors écran : le curseur disparaît pendant la lecture */
    outb(VGA_CRTC_INDEX, 14);
    outb(VGA_CRTC_DATA, (u8)(pos >> 8));
    outb(VGA_CRTC_INDEX, 15);
    outb(VGA_CRTC_DATA, (u8)(pos & 0xFF));
}

/* Ligne virtuelle i : 0 = la plus ancienne conservée ... ring_count-1, puis les 25 lignes de l'écran. */
static const u16 *virtual_line(usize i)
{
    if (i < ring_count)
        return ring[(ring_head + SCROLLBACK_LINES - ring_count + i) % SCROLLBACK_LINES];
    return live[i - ring_count];
}

static void redraw(void)
{
    usize first = ring_count - view_offset;     /* première ligne virtuelle affichée */

    for (usize y = 0; y < VGA_HEIGHT; y++) {
        const u16 *line = virtual_line(first + y);

        for (usize x = 0; x < VGA_WIDTH; x++)
            VGA_MEMORY[y * VGA_WIDTH + x] = line[x];
    }
}

/* Écrit une cellule de l'écran vivant (et à l'écran si on est en bas). */
static void put_cell(usize y, usize x, u16 value)
{
    live[y][x] = value;
    if (!view_offset)
        VGA_MEMORY[y * VGA_WIDTH + x] = value;
}

void vga_scroll_reset(void)
{
    if (view_offset) {
        view_offset = 0;
        redraw();
        cursor_update();
    }
}

void vga_scroll_view(int lines)
{
    long target = (long)view_offset + lines;

    if (target < 0)
        target = 0;
    if (target > (long)ring_count)
        target = (long)ring_count;
    if ((usize)target == view_offset)
        return;
    view_offset = (usize)target;
    redraw();
    cursor_update();
}

usize vga_scrollback_lines(void)
{
    return ring_count;
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
    view_offset = 0;
    for (usize y = 0; y < VGA_HEIGHT; y++)
        for (usize x = 0; x < VGA_WIDTH; x++)
            live[y][x] = entry(' ', color);
    redraw();
    row = col = 0;
    cursor_update();
}

void vga_init(void)
{
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    ring_count = ring_head = 0;
    vga_clear();
}

static void scroll(void)
{
    /* la ligne du haut part dans l'historique */
    for (usize x = 0; x < VGA_WIDTH; x++)
        ring[ring_head][x] = live[0][x];
    ring_head = (ring_head + 1) % SCROLLBACK_LINES;
    if (ring_count < SCROLLBACK_LINES)
        ring_count++;

    for (usize y = 1; y < VGA_HEIGHT; y++)
        for (usize x = 0; x < VGA_WIDTH; x++)
            live[y - 1][x] = live[y][x];
    for (usize x = 0; x < VGA_WIDTH; x++)
        live[VGA_HEIGHT - 1][x] = entry(' ', color);

    redraw();                                   /* view_offset vaut 0 ici : on affiche l'écran vivant */
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
    vga_scroll_reset();                         /* écrire ramène en bas */

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
        put_cell(row, col, entry(' ', color));
        break;
    case '\t':
        do {
            put_cell(row, col, entry(' ', color));
            if (++col == VGA_WIDTH) {
                newline();
                break;
            }
        } while (col % 4);
        break;
    default:
        put_cell(row, col, entry(c, color));
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
