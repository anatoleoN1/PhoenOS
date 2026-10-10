/* Test du vrai keyboard.c : scancodes simulés -> caractères AZERTY. */
#include "testlib.h"
#include "isr.h"
#include "keyboard.h"

u8 test_scancode;
static irq_handler_t kbd_handler;

err_t irq_install_handler(u8 irq, irq_handler_t handler)
{
    if (irq == 1)
        kbd_handler = handler;
    return E_OK;
}

static void sc(u8 code)
{
    test_scancode = code;
    kbd_handler(NULL);
}

/* Appuie sur une touche (make + break) et renvoie ce que le pilote a produit. */
static int tap(u8 make)
{
    sc(make);
    sc((u8)(make | 0x80));
    return keyboard_try_getc();
}

static int tap_ext(u8 make)
{
    sc(0xE0); sc(make);
    sc(0xE0); sc((u8)(make | 0x80));
    return keyboard_try_getc();
}

int test_main(void)
{
    CHECK(keyboard_init() == E_OK && kbd_handler);
    CHECK(keyboard_try_getc() == -1);

    /* lettres : AZERTY (A en haut à gauche du clavier QWERTY = scancode 0x10) */
    CHECK(tap(0x10) == 'a' && tap(0x11) == 'z' && tap(0x12) == 'e' && tap(0x1E) == 'q');
    CHECK(tap(0x27) == 'm' && tap(0x2C) == 'w' && tap(0x32) == ',' && tap(0x33) == ';');
    CHECK(keyboard_try_getc() == -1);                           /* le relâchement ne produit rien */

    /* rangée des chiffres sans Maj : symboles et accents */
    CHECK(tap(0x02) == '&' && tap(0x03) == 0x82 && tap(0x04) == '"' && tap(0x05) == '\'');
    CHECK(tap(0x06) == '(' && tap(0x07) == '-' && tap(0x08) == 0x8A && tap(0x09) == '_');
    CHECK(tap(0x0A) == 0x87 && tap(0x0B) == 0x85 && tap(0x0C) == ')' && tap(0x0D) == '=');
    CHECK(tap(0x28) == 0x97 && tap(0x29) == 0xFD && tap(0x56) == '<');

    /* Maj gauche et droite */
    sc(0x2A);
    CHECK(tap(0x10) == 'A' && tap(0x02) == '1' && tap(0x0B) == '0' && tap(0x56) == '>');
    CHECK(tap(0x33) == '.' && tap(0x34) == '/' && tap(0x32) == '?');
    sc(0xAA);
    CHECK(tap(0x02) == '&');
    sc(0x36);
    CHECK(tap(0x11) == 'Z');
    sc(0xB6);
    CHECK(tap(0x11) == 'z');

    /* AltGr : ~ # { [ | ` \ ^ @ ] } */
    sc(0xE0); sc(0x38);
    CHECK(tap(0x03) == '~' && tap(0x04) == '#' && tap(0x05) == '{' && tap(0x06) == '[');
    CHECK(tap(0x07) == '|' && tap(0x08) == '`' && tap(0x09) == '\\' && tap(0x0A) == '^');
    CHECK(tap(0x0B) == '@' && tap(0x0C) == ']' && tap(0x0D) == '}');
    sc(0xE0); sc(0xB8);
    CHECK(tap(0x03) == 0x82);                                   /* AltGr relâché : é revient */

    /* Verr. Maj : n'agit que sur les lettres */
    sc(0x3A); sc(0xBA);
    CHECK(tap(0x10) == 'A' && tap(0x02) == '&');
    sc(0x2A);
    CHECK(tap(0x10) == 'a');                                    /* Maj inverse Verr. Maj */
    sc(0xAA);
    sc(0x3A); sc(0xBA);
    CHECK(tap(0x10) == 'a');

    /* Ctrl + lettre */
    sc(0x1D);
    CHECK(tap(0x2E) == 3 && tap(0x26) == 12 && tap(0x16) == 21 && tap(0x10) == 1);
    sc(0x9D);
    CHECK(tap(0x2E) == 'c');

    /* touches de contrôle */
    CHECK(tap(0x1C) == '\n' && tap(0x0E) == '\b' && tap(0x0F) == '\t' && tap(0x39) == ' ');
    CHECK(tap(0x01) == 0x1B);

    /* touches spéciales (préfixe E0) */
    CHECK(tap_ext(0x48) == KEY_UP && tap_ext(0x50) == KEY_DOWN);
    CHECK(tap_ext(0x4B) == KEY_LEFT && tap_ext(0x4D) == KEY_RIGHT);
    CHECK(tap_ext(0x47) == KEY_HOME && tap_ext(0x4F) == KEY_END && tap_ext(0x53) == KEY_DEL);
    CHECK(tap_ext(0x1C) == '\n' && tap_ext(0x35) == '/');       /* pavé numérique */
    CHECK(tap_ext(0x49) == KEY_PGUP && tap_ext(0x51) == KEY_PGDN);   /* Page haut / bas */
    sc(0x1D);                                                   /* Ctrl enfonce */
    CHECK(tap_ext(0x48) == KEY_PGUP && tap_ext(0x50) == KEY_PGDN);   /* Ctrl+haut / bas */
    sc(0x9D);
    sc(0x38);                                                   /* Alt enfonce */
    CHECK(tap_ext(0x48) == KEY_PGUP && tap_ext(0x50) == KEY_PGDN);
    sc(0xB8);
    CHECK(tap_ext(0x48) == KEY_UP && tap_ext(0x50) == KEY_DOWN);     /* relaches : fleches normales */

    /* faux Maj envoyé avant ImprEcran (E0 2A) : ne doit pas bloquer la Maj */
    sc(0xE0); sc(0x2A);
    CHECK(tap(0x10) == 'a');
    sc(0xE0); sc(0xAA);

    /* touche Pause : séquence de 6 octets à ignorer, sans laisser Ctrl enfoncé */
    sc(0xE1); sc(0x1D); sc(0x45); sc(0xE1); sc(0x9D); sc(0xC5);
    CHECK(keyboard_try_getc() == -1);
    CHECK(tap(0x2E) == 'c');

    /* tampon plein : les touches en trop sont perdues, rien ne plante */
    for (int i = 0; i < 300; i++) {
        sc(0x10);                                               /* sans lire : le tampon se remplit */
        sc(0x90);
    }
    {
        int n = 0;

        while (keyboard_try_getc() != -1)
            n++;
        CHECK(n == 127);
    }
    CHECK(tap(0x11) == 'z');                                    /* le clavier repart normalement */

    /* ---- console série (terminal où tourne QEMU) ---- */
    {
        static const struct { const char *in; int out[6]; } cases[] = {
            { "ls\r",            { 'l', 's', '\n', -1 } },
            { "a\r\nb",          { 'a', '\n', 'b', -1 } },           /* \r\n : une seule Entrée */
            { "a\n\nb",          { 'a', '\n', '\n', 'b', -1 } },     /* deux \n = deux Entrées */
            { "x\x7f\x08",       { 'x', '\b', '\b', -1 } },         /* Retour arriere : 0x7F ou 0x08 */
            { "\t\x03\x0c\x15", { '\t', 3, 12, 21, -1 } },         /* Tab, Ctrl+C, Ctrl+L, Ctrl+U */
            { "\x1b[A\x1b[B",    { KEY_UP, KEY_DOWN, -1 } },
            { "\x1b[C\x1b[D",    { KEY_RIGHT, KEY_LEFT, -1 } },
            { "\x1b[H\x1b[F",    { KEY_HOME, KEY_END, -1 } },
            { "\x1b[5~\x1b[6~",  { KEY_PGUP, KEY_PGDN, -1 } },
            { "\x1b[3~",         { KEY_DEL, -1 } },
            { "\x1bOA\x1bOD",    { KEY_UP, KEY_LEFT, -1 } },          /* mode application */
            { "\x1b[1;5C",       { KEY_RIGHT, -1 } },                 /* avec modificateur : touche de base */
            { "\x1b[1;5A\x1b[1;5B", { KEY_PGUP, KEY_PGDN, -1 } },      /* Ctrl+haut / bas : defilement */
            { "\x1b[1;3A\x1b[1;3B", { KEY_PGUP, KEY_PGDN, -1 } },      /* Alt+haut / bas */
            { "\x1b[1;2A\x1b[1;2B", { KEY_PGUP, KEY_PGDN, -1 } },      /* Maj+haut / bas */
            { "\x1b[A\x1b[1;5A\x1b[B", { KEY_UP, KEY_PGUP, KEY_DOWN, -1 } },   /* le modificateur ne "colle" pas */
            { "\x1b[5;5~\x1b[6;2~", { KEY_PGUP, KEY_PGDN, -1 } },      /* PageHaut avec modificateur */
            { "\x1b[3;5~",       { KEY_DEL, -1 } },
            { "\x02\x06",        { 2, 6, -1 } },                      /* Ctrl+B / Ctrl+F : le shell les traduit */
            { "\x1bx",           { 'x', -1 } },                       /* ESC isole : ignore */
            { "\xc3\xa9\xc3\xa8\xc3\xa0\xc3\xa7", { 0x82, 0x8A, 0x85, 0x87, -1 } },   /* é è à ç */
            { "\xc3\x01z",       { 'z', -1 } },                       /* UTF-8 inconnu : ignore sans bloquer */
            { "\xff\xfe\x80q",   { 'q', -1 } },                       /* octets parasites ignores */
        };

        for (usize i = 0; i < sizeof cases / sizeof cases[0]; i++) {
            for (const char *p = cases[i].in; *p; p++)
                keyboard_feed_serial((u8)*p);
            for (int j = 0;; j++) {
                int got = keyboard_try_getc();

                if (cases[i].out[j] == -1) {
                    if (got != -1) { out("  ECHEC serie cas "); outnum((u32)i); out(" : touche en trop\n"); test_fails++; }
                    break;
                }
                if (got != cases[i].out[j]) {
                    out("  ECHEC serie cas "); outnum((u32)i); out(" : attendu "); outnum((u32)cases[i].out[j]);
                    out(" obtenu "); outnum((u32)(got + 1)); out("-1\n");
                    test_fails++;
                    while (keyboard_try_getc() != -1) ;
                    break;
                }
            }
        }
        /* un octet de séquence coupée en deux morceaux par le réseau/terminal */
        keyboard_feed_serial(0x1b); keyboard_feed_serial('[');
        CHECK(keyboard_try_getc() == -1);
        keyboard_feed_serial('A');
        CHECK(keyboard_try_getc() == KEY_UP);
        /* flot d'octets au hasard : jamais de blocage ni de plantage */
        {
            u32 x = 12345;

            for (int i = 0; i < 20000; i++) {
                x = x * 1103515245u + 12345u;
                keyboard_feed_serial((u8)(x >> 16));
                if (i % 7 == 0) (void)keyboard_try_getc();
            }
            while (keyboard_try_getc() != -1) ;
            keyboard_feed_serial('o'); keyboard_feed_serial('k');
            keyboard_feed_serial(0x1b); keyboard_feed_serial('x');                  /* sortir de tout etat intermediaire */
            while (keyboard_try_getc() != -1) ;
            keyboard_feed_serial('o');
            CHECK(keyboard_try_getc() == 'o');
        }
    }

    out("  clavier AZERTY verifie : lettres, chiffres, AltGr, Maj, Verr. Maj, Ctrl, fleches\n");
    return 0;
}
