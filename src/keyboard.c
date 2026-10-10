/*
 * PhoenOS - keyboard.c
 * Pilote clavier PS/2 : scancodes "set 1" -> caractères, disposition AZERTY.
 */
#include "keyboard.h"
#include "isr.h"
#include "io.h"
#include "serial.h"

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

static volatile u16 buffer[KBD_BUF_SIZE];
static volatile u32 head, tail;

static int extended, pause_skip;
static int shift, ctrl, alt, altgr, caps;

static void buffer_push(u16 c)
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

    /* Touche Pause : E1 1D 45 E1 9D C5. Le test du compteur doit passer AVANT celui de E1,
     * sinon le second E1 relancerait le décompte et avalerait les touches suivantes. */
    if (pause_skip) { pause_skip--; return; }
    if (sc == 0xE1) { pause_skip = 5; return; }
    if (sc == 0xE0) { extended = 1; return; }

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

    if (release)
        return;

    if (ext) {                                  /* bloc de navigation (touches préfixées E0) */
        switch (sc) {
        case 0x48: buffer_push((ctrl || alt) ? KEY_PGUP : KEY_UP);   break;   /* Ctrl/Alt+haut : page precedente */
        case 0x50: buffer_push((ctrl || alt) ? KEY_PGDN : KEY_DOWN); break;
        case 0x4B: buffer_push(KEY_LEFT);  break;
        case 0x4D: buffer_push(KEY_RIGHT); break;
        case 0x47: buffer_push(KEY_HOME);  break;
        case 0x4F: buffer_push(KEY_END);   break;
        case 0x53: buffer_push(KEY_DEL);   break;
        case 0x49: buffer_push(KEY_PGUP);  break;
        case 0x51: buffer_push(KEY_PGDN);  break;
        case 0x35: buffer_push('/');       break;   /* pavé numérique : / */
        case 0x1C: buffer_push('\n');      break;   /* pavé numérique : Entrée */
        default:   break;
        }
        return;
    }

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

/*
 * Console série : les octets reçus sur COM1 (le terminal où l'on a lancé QEMU avec -serial stdio)
 * sont convertis en touches, comme celles du clavier PS/2. Permet d'utiliser PhoenOS sans fenêtre
 * graphique (ex. `make serial`) ou quand la fenêtre de QEMU ne reçoit pas le clavier.
 * Reconnu : Entrée (\r ou \n), Retour arrière (0x08 ou 0x7F), Tab, Ctrl+lettre, flèches, Début, Fin,
 * Suppr, PageHaut, PageBas (séquences ESC [ ... et ESC O ...), lettres accentuées UTF-8 (é è à ç...).
 */
enum { S_NORMAL, S_ESC, S_CSI, S_SS3, S_UTF8 };
static int s_state, s_param, s_first, s_semi, s_after_cr;
static u8 s_lead;

static void serial_csi_final(u8 f)
{
    int number = s_semi ? s_first : s_param;    /* "5~" ou "5;2~" : le numero de touche */
    int mod    = s_semi ? s_param : 0;          /* "1;5A" : 2 = Maj, 3 = Alt, 5 = Ctrl... */

    switch (f) {
    /* Maj/Alt/Ctrl + fleche haut/bas = page precedente/suivante (claviers sans PageHaut/PageBas) */
    case 'A': buffer_push(mod >= 2 ? KEY_PGUP : KEY_UP);    break;
    case 'B': buffer_push(mod >= 2 ? KEY_PGDN : KEY_DOWN);  break;
    case 'C': buffer_push(KEY_RIGHT); break;
    case 'D': buffer_push(KEY_LEFT);  break;
    case 'H': buffer_push(KEY_HOME);  break;
    case 'F': buffer_push(KEY_END);   break;
    case '~':
        switch (number) {
        case 1: case 7: buffer_push(KEY_HOME); break;
        case 3:         buffer_push(KEY_DEL);  break;
        case 4: case 8: buffer_push(KEY_END);  break;
        case 5:         buffer_push(KEY_PGUP); break;
        case 6:         buffer_push(KEY_PGDN); break;
        default: break;
        }
        break;
    default: break;
    }
}

/* UTF-8 (2 octets) -> CP437 pour les lettres françaises ; 0 si inconnu. */
static u8 utf8_to_cp437(u8 lead, u8 cont)
{
    static const struct { u8 lead, cont, cp; } map[] = {
        { 0xC3, 0x87, 0x80 }, { 0xC3, 0xBC, 0x81 }, { 0xC3, 0xA9, 0x82 }, { 0xC3, 0xA2, 0x83 },
        { 0xC3, 0xA4, 0x84 }, { 0xC3, 0xA0, 0x85 }, { 0xC3, 0xA7, 0x87 }, { 0xC3, 0xAA, 0x88 },
        { 0xC3, 0xAB, 0x89 }, { 0xC3, 0xA8, 0x8A }, { 0xC3, 0xAF, 0x8B }, { 0xC3, 0xAE, 0x8C },
        { 0xC3, 0xB4, 0x93 }, { 0xC3, 0xB6, 0x94 }, { 0xC3, 0xBB, 0x96 }, { 0xC3, 0xB9, 0x97 },
        { 0xC2, 0xB0, 0xF8 }, { 0xC2, 0xA3, 0x9C }, { 0xC2, 0xB5, 0xE6 },
    };

    for (usize i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (map[i].lead == lead && map[i].cont == cont)
            return map[i].cp;
    return 0;
}

void keyboard_feed_serial(u8 b)
{
    int was_cr = s_after_cr;

    s_after_cr = 0;
    switch (s_state) {
    case S_ESC:
        if (b == '[') { s_state = S_CSI; s_param = s_first = s_semi = 0; return; }
        if (b == 'O') { s_state = S_SS3; s_param = s_first = s_semi = 0; return; }
        s_state = S_NORMAL;                     /* ESC isolé suivi d'autre chose : on ignore l'ESC et on traite l'octet */
        break;
    case S_CSI:
        if (b >= '0' && b <= '9') { s_param = s_param * 10 + (b - '0'); if (s_param > 99) s_param = 99; return; }
        if (b == ';') { s_first = s_param; s_param = 0; s_semi = 1; return; }
        s_state = S_NORMAL;
        serial_csi_final(b);
        return;
    case S_SS3:
        s_state = S_NORMAL;
        serial_csi_final(b);
        return;
    case S_UTF8: {
        u8 cp;

        s_state = S_NORMAL;
        cp = utf8_to_cp437(s_lead, b);
        if (cp)
            buffer_push(cp);
        return;
    }
    default:
        break;
    }

    if (b == 0x1B) { s_state = S_ESC; return; }
    if (b == '\r') { s_after_cr = 1; buffer_push('\n'); return; }
    if (b == '\n') { if (!was_cr) buffer_push('\n'); return; }      /* \r\n : une seule Entrée */
    if (b == 0x7F || b == 0x08) { buffer_push('\b'); return; }
    if (b == 0xC2 || b == 0xC3) { s_state = S_UTF8; s_lead = b; return; }
    if (b == '\t' || (b >= 1 && b <= 26) || (b >= 32 && b < 127))
        buffer_push(b);                                                /* Ctrl+lettre = 1..26 comme au clavier */
}

/* Lit les octets en attente sur COM1. À appeler avec les interruptions coupées. */
static void serial_poll(void)
{
    int c;

    for (int n = 0; n < 64 && (c = serial_try_getc()) >= 0; n++)
        keyboard_feed_serial((u8)c);
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
    serial_poll();
    if (head != tail) {
        c = buffer[tail];
        tail = (tail + 1) % KBD_BUF_SIZE;
    }
    cpu_sti();
    return c;
}

int keyboard_getc(void)
{
    for (;;) {
        cpu_cli();
        serial_poll();                          /* le terminal série est consulté à chaque réveil (100 fois par seconde) */
        if (head != tail) {
            int c = buffer[tail];
            tail = (tail + 1) % KBD_BUF_SIZE;
            cpu_sti();
            return c;
        }
        /* sti + hlt : aucune interruption ne peut se glisser entre les deux */
        __asm__ volatile ("sti; hlt");
    }
}
