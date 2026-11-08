/* Decodeur de souris PS/2 (mouse.c) : paquets de 3 et 4 octets, signes, bornes, resynchronisation, debordement, molette. */
#include "testlib.h"
#include "mouse.h"
u8 test_scancode;

static int feed3(u8 b0, u8 dx, u8 dy)
{
    int r = mouse_feed(b0);

    r |= mouse_feed(dx);
    return r | mouse_feed(dy);
}

int test_main(void)
{
    struct mouse_state m;

    CHECK(!mouse_present());
    mouse_set_present(1);
    CHECK(mouse_present());
    mouse_reset_decoder(0);
    mouse_set_bounds(800, 600);
    mouse_get(&m);
    CHECK(m.x == 400 && m.y == 300 && m.buttons == 0 && m.events == 0);        /* au milieu de l'ecran au depart */

    CHECK(mouse_feed(0x08) == 0 && mouse_feed(10) == 0 && mouse_feed(5) == 1);   /* un paquet complet : +10 en x, 5 vers le haut */
    mouse_get(&m);
    CHECK(m.x == 410 && m.y == 295 && m.events == 1);                           /* l'axe Y de l'ecran descend */

    feed3(0x08 | 0x10 | 0x20, 0xF6, 0xFB);                                       /* -10, -5 (signe etendu) : gauche + bas */
    mouse_get(&m);
    CHECK(m.x == 400 && m.y == 300);

    feed3(0x08 | 1 | 4, 0, 0);                                                   /* boutons gauche + milieu */
    mouse_get(&m);
    CHECK(m.buttons == (MOUSE_LEFT | MOUSE_MIDDLE));
    feed3(0x08 | 2, 0, 0);
    mouse_get(&m);
    CHECK(m.buttons == MOUSE_RIGHT);

    for (int i = 0; i < 100; i++)                                                /* bornes */
        feed3(0x08 | 0x10, 0x80, 0x00);
    mouse_get(&m);
    CHECK(m.x == 0 && m.y == 300);
    for (int i = 0; i < 100; i++)
        feed3(0x08, 127, 127);
    mouse_get(&m);
    CHECK(m.x == 799 && m.y == 0);
    for (int i = 0; i < 100; i++)
        feed3(0x08 | 0x20, 0, 0x80);
    mouse_get(&m);
    CHECK(m.y == 599);

    {   /* resynchronisation : un octet sans bit 3 en debut de paquet est ignore */
        u32 ev;

        mouse_get(&m);
        ev = m.events;
        CHECK(mouse_feed(0x00) == 0);
        CHECK(feed3(0x08, 1, 0) == 1);
        mouse_get(&m);
        CHECK(m.events == ev + 1);
    }
    {   /* debordement : paquet compte comme evenement mais ne bouge pas */
        i32 x, y;

        mouse_get(&m);
        x = m.x; y = m.y;
        feed3(0x08 | 0x40, 50, 50);
        mouse_get(&m);
        CHECK(m.x == x && m.y == y);
    }

    /* molette : paquets de 4 octets */
    mouse_reset_decoder(1);
    CHECK(mouse_has_wheel());
    mouse_get(&m);
    mouse_feed(0x08); mouse_feed(0); mouse_feed(0);
    CHECK(mouse_feed(0x0F) == 1);                                                /* -1 : vers le haut */
    mouse_feed(0x08); mouse_feed(0); mouse_feed(0);
    mouse_feed(0x02);
    mouse_get(&m);
    CHECK(m.wheel == 1);
    mouse_get(&m);
    CHECK(m.wheel == 0);                                                         /* remise a zero apres lecture */

    mouse_reset_decoder(0);
    mouse_set_bounds(100, 100);                                                  /* ecran plus petit : position ramenee dedans */
    mouse_get(&m);
    CHECK(m.x == 99 && m.y == 99);
    mouse_set_bounds(0, -5);
    mouse_get(&m);
    CHECK(m.x == 0 && m.y == 0);
    out("  souris : decodage, signes, bornes, resynchronisation, debordement, molette\n");
    return 0;
}
