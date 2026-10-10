/* Test du vrai vga.c (avec fausse mémoire vidéo) : texte, défilement, historique de l'écran. */
#include "testlib.h"
#include "vga.h"

u8 test_scancode;
volatile u16 test_vga[80 * 25];

static void line(int n)                         /* écrit "L<nnnn>" + retour à la ligne */
{
    char b[8] = { 'L', (char)('0' + n / 1000 % 10), (char)('0' + n / 100 % 10), (char)('0' + n / 10 % 10),
                  (char)('0' + n % 10), '\n', 0 };

    vga_puts(b);
}

/* Numéro de la ligne affichée sur la rangée y (-1 si ce n'est pas "Lnnnn"). */
static int shown(int y)
{
    int v = 0;

    if ((test_vga[y * 80] & 0xFF) != 'L')
        return -1;
    for (int i = 1; i <= 4; i++)
        v = v * 10 + (int)((test_vga[y * 80 + i] & 0xFF) - '0');
    return v;
}

int test_main(void)
{
    vga_init();
    vga_set_color(VGA_YELLOW, VGA_BLUE);

    /* écriture simple et attributs */
    vga_puts("ab");
    CHECK((test_vga[0] & 0xFF) == 'a' && (test_vga[1] >> 8) == (VGA_YELLOW | (VGA_BLUE << 4)));
    vga_putc('\b');
    CHECK((test_vga[1] & 0xFF) == ' ');
    vga_clear();
    CHECK(vga_scrollback_lines() == 0);

    /* 100 lignes : l'écran garde les 24 dernières, 76 sont dans l'historique */
    for (int i = 0; i < 100; i++)
        line(i);
    CHECK(vga_scrollback_lines() == 100 + 1 - 25);
    CHECK(shown(0) == 76 && shown(23) == 99 && (test_vga[24 * 80] & 0xFF) == ' ');

    /* remonter / redescendre */
    vga_scroll_view(12);
    CHECK(shown(0) == 64 && shown(23) == 87);
    vga_scroll_view(12);
    CHECK(shown(0) == 52);
    vga_scroll_view(-24);
    CHECK(shown(0) == 76 && shown(23) == 99);
    vga_scroll_view(-5);                                        /* déjà en bas : sans effet */
    CHECK(shown(0) == 76);

    /* limites */
    vga_scroll_view(100000);
    CHECK(shown(0) == 0 && shown(24) == 24);                    /* tout en haut : L0000 */
    vga_scroll_view(100000);
    CHECK(shown(0) == 0);
    vga_scroll_view(-100000);
    CHECK(shown(0) == 76);

    /* écrire pendant la lecture ramène en bas, sans perdre la ligne en cours */
    vga_scroll_view(30);
    CHECK(shown(0) == 46);
    vga_putc('Z');
    CHECK(shown(0) == 76 && (test_vga[24 * 80] & 0xFF) == 'Z');
    vga_scroll_reset();                                         /* déjà en bas : sans effet */
    CHECK(shown(0) == 76);

    /* vga_clear : l'écran est vide mais l'historique reste lisible */
    vga_clear();
    CHECK((test_vga[0] & 0xFF) == ' ');
    CHECK(vga_scrollback_lines() == 76);
    vga_scroll_view(1);
    CHECK((test_vga[0] & 0xFF) != ' ');                         /* l'historique est bien affiche */
    vga_scroll_reset();

    /* débordement du tampon circulaire : 500 lignes conservées au maximum */
    vga_init();
    for (int i = 0; i < 1200; i++)
        line(i);
    CHECK(vga_scrollback_lines() == 500);
    CHECK(shown(0) == 1200 + 1 - 25);
    vga_scroll_view(500);
    CHECK(shown(0) == 1200 + 1 - 25 - 500 && shown(1) == shown(0) + 1);
    vga_scroll_view(-500);
    CHECK(shown(23) == 1199);

    /* défilement doux dans la plage : continuité (aucune ligne manquante) */
    vga_scroll_view(37);
    {
        int ok = 1, first = shown(0);

        for (int y = 1; y < 25; y++)
            if (shown(y) != first + y)
                ok = 0;
        CHECK(ok);
    }
    vga_scroll_reset();

    /* retour chariot, tabulation, longue ligne qui passe à la ligne */
    vga_init();
    vga_puts("\tX");
    CHECK((test_vga[4] & 0xFF) == 'X');
    for (int i = 0; i < 85; i++)
        vga_putc('q');
    CHECK((test_vga[79] & 0xFF) == 'q' && (test_vga[80 + 9] & 0xFF) == 'q' && (test_vga[80 + 10] & 0xFF) == ' ');

    out("  ecran verifie : texte, historique de 500 lignes, defilement, limites\n");
    return 0;
}
