/* Tests de cmdline.c : découpage de ligne, nombres, historique. */
#include "testlib.h"
#include "cmdline.h"

u8 test_scancode;

static int split(const char *src, char **argv, int max)
{
    static char buf[256];

    memcpy(buf, src, strlen(src) + 1);
    return cmdline_split(buf, argv, max);
}

static u32 num(const char *s, err_t *e)
{
    u32 v = 0xDEAD;

    *e = cmdline_parse_u32(s, &v);
    return v;
}

int test_main(void)
{
    char *a[16];
    err_t e;
    struct history h;
    const char *s;

    /* --- découpage --- */
    CHECK(split("echo hello world", a, 8) == 3 && !strcmp(a[0], "echo") && !strcmp(a[2], "world") && a[3] == NULL);
    CHECK(split("   \t  a \t b  ", a, 8) == 2 && !strcmp(a[0], "a") && !strcmp(a[1], "b"));
    CHECK(split("", a, 8) == 0 && split("    ", a, 8) == 0);
    CHECK(split("echo \"a   b\" c", a, 8) == 3 && !strcmp(a[1], "a   b") && !strcmp(a[2], "c"));
    CHECK(split("echo \"\"", a, 8) == 2 && !strcmp(a[1], ""));
    CHECK(split("echo \"non ferme", a, 8) == E_INVAL);
    CHECK(split("1 2 3 4 5 6 7 8", a, 8) == 8 && !strcmp(a[7], "8") && a[8] == NULL);
    CHECK(split("1 2 3 4 5 6 7 8 9", a, 8) == E_2BIG);
    CHECK(split("caf\x82 \x85", a, 8) == 2);                    /* caractères accentués (CP437) */

    /* --- nombres --- */
    CHECK(num("0", &e) == 0 && e == E_OK);
    CHECK(num("123", &e) == 123 && e == E_OK);
    CHECK(num("0x1F", &e) == 31 && e == E_OK);
    CHECK(num("0Xff", &e) == 255 && e == E_OK);
    CHECK(num("4294967295", &e) == 0xFFFFFFFFu && e == E_OK);
    CHECK(num("0xFFFFFFFF", &e) == 0xFFFFFFFFu && e == E_OK);
    num("4294967296", &e);    CHECK(e == E_INVAL);
    num("99999999999", &e);   CHECK(e == E_INVAL);
    num("0x100000000", &e);   CHECK(e == E_INVAL);
    num("", &e);              CHECK(e == E_INVAL);
    num("abc", &e);           CHECK(e == E_INVAL);
    num("12a", &e);           CHECK(e == E_INVAL);
    num("-1", &e);            CHECK(e == E_INVAL);
    num("0x", &e);            CHECK(e == E_INVAL);
    num("0xG", &e);           CHECK(e == E_INVAL);
    CHECK(cmdline_parse_u32(NULL, (u32[1]){0}) == E_INVAL);
    { u32 keep = 77; cmdline_parse_u32("zz", &keep); CHECK(keep == 77); }   /* sortie intacte si erreur */

    /* --- historique --- */
    history_init(&h);
    CHECK(history_prev(&h) == NULL && history_next(&h) == NULL && history_at_end(&h));
    history_add(&h, "");
    CHECK(h.count == 0);
    history_add(&h, "a"); history_add(&h, "b"); history_add(&h, "c");
    history_add(&h, "c");                                       /* doublon consécutif ignoré */
    CHECK(h.count == 3 && !strcmp(history_get(&h, 0), "a") && !strcmp(history_get(&h, 2), "c"));
    CHECK(history_get(&h, 3) == NULL && history_get(&h, -1) == NULL);

    CHECK(history_at_end(&h));
    s = history_prev(&h); CHECK(s && !strcmp(s, "c") && !history_at_end(&h));
    s = history_prev(&h); CHECK(s && !strcmp(s, "b"));
    s = history_prev(&h); CHECK(s && !strcmp(s, "a"));
    CHECK(history_prev(&h) == NULL);                            /* déjà à la plus ancienne */
    s = history_next(&h); CHECK(s && !strcmp(s, "b"));
    s = history_next(&h); CHECK(s && !strcmp(s, "c"));
    s = history_next(&h); CHECK(s && !strcmp(s, "") && history_at_end(&h));
    CHECK(history_next(&h) == NULL);

    history_prev(&h);                                           /* on navigue... */
    history_add(&h, "d");                                       /* ...puis une commande remet à la fin */
    CHECK(history_at_end(&h) && h.count == 4);

    history_init(&h);
    for (int i = 0; i < 20; i++) {
        char c[16] = "cmd";
        c[3] = (char)('A' + i);
        c[4] = 0;
        history_add(&h, c);
    }
    CHECK(h.count == HIST_MAX && !strcmp(history_get(&h, 0), "cmdE") && !strcmp(history_get(&h, HIST_MAX - 1), "cmdT"));
    {
        static char longline[HIST_LEN + 10];

        memset(longline, 'x', sizeof longline - 1);
        longline[sizeof longline - 1] = 0;
        history_add(&h, longline);                              /* trop longue : ignorée */
        CHECK(h.count == HIST_MAX && !strcmp(history_get(&h, HIST_MAX - 1), "cmdT"));
    }

    out("  decoupage, nombres et historique verifies\n");
    return 0;
}
