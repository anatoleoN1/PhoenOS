/*
 * PhoenOS - keyboard.c
 * Pilote clavier PS/2 : scancodes "set 1" -> caractères, disposition AZERTY.
 */
#include "keyboard.h"
#include "isr.h"
#include "io.h"

#define KBD_DATA     0x60
#define KBD_STATUS   0x64
#define KBD_BUF_SIZE 128

/* Caractères CP437 utilisés par la disposition française */
#define C_E_ACUTE   0x82   /* é */
#define C_E_GRAVE   0x8A   /* è */
#define C_A_GRAVE   0x85   /* à */
#define C_C_CEDIL   0x87   /* ç */
#define C_U_GRAVE   0x97   /* ù */
#define C_DEGREE    0xF8   /* ° */
#define C_POUND     0x9C   /* £ */
#define C_MICRO     0xE6   /* µ */
#define C_SQUARE    0xFD   /* ² */
#define C_SECTION   0x15   /* § */

static const u8 layout_normal[128] = {
    [0x01] = 0x1B,
    [0x02] = '&',       [0x03] = C_E_ACUTE, [0x04] = '"',       [0x05] = '\'',
    [0x06] = '(',       [0x07] = '-',       [0x08] = C_E_GRAVE, [0x09] = '_',
    [0x0A] = C_C_CEDIL, [0x0B] = C_A_GRAVE, [0x0C] = ')',       [0x0D] = '=',
    [0x0E] = '\b',      [0x0F] = '\t',
    [0x10] = 'a', [0x11] = 'z', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '^', [0x1B] = '$', [0x1C] = '\n',
    [0x1E] = 'q', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = 'm',
    [0x28] = C_U_GRAVE, [0x29] = C_SQUARE,
    [0x2B] = '*',
    [0x2C] = 'w', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = ',', [0x33] = ';', [0x34] = ':', [0x35] = '!',
    [0x39] = ' ',
    [0x56] = '<',
};

static const u8 layout_shift[128] = {
    [0x01] = 0x1B,
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = C_DEGREE, [0x0D] = '+',
    [0x0E] = '\b',     [0x0F] = '\t',
    [0x10] = 'A', [0x11] = 'Z', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T',
    [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
    [0x1A] = '"', [0x1B] = C_POUND, [0x1C] = '\n',
    [0x1E] = 'Q', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G',
    [0x23] = 'H', [0x24] = 'J', [0x25] = 'K', [0x26] = 'L', [0x27] = 'M',
    [0x28] = '%',
    [0x2B] = C_MICRO,
    [0x2C] = 'W', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B',
    [0x31] = 'N', [0x32] = '?', [0x33] = '.', [0x34] = '/', [0x35] = C_SECTION,
    [0x39] = ' ',
    [0x56] = '>',
};

/* AltGr + touche de la rangée des chiffres (programmation : ~ # { [ | ` \ ^ @ ] }) */
static const u8 layout_altgr[128] = {
    [0x03] = '~', [0x04] = '#', [0x05] = '{', [0x06] = '[', [0x07] = '|',
    [0x08] = '`', [0x09] = '\\', [0x0A] = '^', [0x0B] = '@', [0x0C] = ']',
    [0x0D] = '}',
};

static volatile u8 buffer[KBD_BUF_SIZE];
static volatile u32 head, tail;

static int extended, pause_skip;
static int shift, ctrl, alt, altgr, caps;

static void buffer_push(u8 c)
{
    u32 next = (head + 1) % KBD_BUF_SIZE;

    if (next == tail)
        return;                     /* tampon plein : la touche est perdue */
    buffer[head] = c;
    head = next;
}

static void keyboard_irq(struct regs *r)
{
    u8 sc, base, c;
    int release, ext, letter, upper;

    (void)r;
    sc = inb(KBD_DATA);

    if (sc == 0xE0) { extended = 1; return; }
    if (sc == 0xE1) { pause_skip = 5; return; }     /* séquence de la touche Pause */
    if (pause_skip) { pause_skip--; return; }

    release  = (sc & 0x80) != 0;
    sc      &= 0x7F;
    ext      = extended;
    extended = 0;

    switch (sc) {
    case 0x2A: case 0x36:                       /* Maj gauche / droite */
        if (!ext) shift = !release;             /* (E0 2A : faux Maj de ImprEcran) */
        return;
    case 0x1D:                                  /* Ctrl gauche / droit */
        ctrl = !release;
        return;
    case 0x38:                                  /* Alt / AltGr (E0 38) */
        if (ext) altgr = !release; else alt = !release;
        return;
    case 0x3A:                                  /* Verr. Maj */
        if (!release && !ext) caps = !caps;
        return;
    default:
        break;
    }

    (void)alt;
    if (release || ext)                         /* flèches, etc. : pas encore gérées */
        return;

    if (altgr && layout_altgr[sc]) {
        buffer_push(layout_altgr[sc]);
        return;
    }

    base   = layout_normal[sc];
    letter = (base >= 'a' && base <= 'z');
    upper  = letter ? (shift ^ caps) : shift;
    c      = upper ? layout_shift[sc] : base;

    if (!c)
        return;
    if (ctrl && letter)
        c = (u8)(base - 'a' + 1);               /* Ctrl+lettre -> code 1..26 */

    buffer_push(c);
}

err_t keyboard_init(void)
{
    /* Vide les octets déjà présents dans le contrôleur */
    for (int i = 0; i < 16 && (inb(KBD_STATUS) & 1); i++)
        (void)inb(KBD_DATA);

    return irq_install_handler(1, keyboard_irq);
}

int keyboard_try_getc(void)
{
    int c = -1;

    cpu_cli();
    if (head != tail) {
        c = buffer[tail];
        tail = (tail + 1) % KBD_BUF_SIZE;
    }
    cpu_sti();
    return c;
}

u8 keyboard_getc(void)
{
    for (;;) {
        cpu_cli();
        if (head != tail) {
            u8 c = buffer[tail];
            tail = (tail + 1) % KBD_BUF_SIZE;
            cpu_sti();
            return c;
        }
        /* sti + hlt : aucune interruption ne peut se glisser entre les deux */
        __asm__ volatile ("sti; hlt");
    }
}
