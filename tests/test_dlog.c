/* Test du vrai dlog.c : anneau, niveaux, port de debogage 0xE9, format. */
#define TEST_HAS_PORTS
#include "testlib.h"
#include "dlog.h"
#include "kprintf.h"

u8 test_scancode;
void vga_putc(char c) { (void)c; }
void serial_putc(char c) { (void)c; }
u32 pit_uptime_ms(void) { static u32 t = 1234; return t += 7; }
void test_panic(const char *fmt, ...) { out("  PANIQUE : "); out(fmt); out("\n"); exit_now(2); }

static int port_present;
static char port_out[8192];
static u32 port_n;
u32 test_port_in(u16 port, int size) { (void)size; return port == 0xE9 && port_present ? 0xE9 : 0; }
void test_port_out(u16 port, u32 v, int size)
{
    (void)size;
    if (port == 0xE9 && port_n + 1 < sizeof port_out)
        port_out[port_n++] = (char)v, port_out[port_n] = 0;
}

static int has(const char *s)
{
    usize n = strlen(s);

    for (u32 i = 0; i + n <= port_n; i++)
        if (!memcmp(port_out + i, s, n))
            return 1;
    return 0;
}

int test_main(void)
{
    char tag[DLOG_TAG_MAX], msg[DLOG_MSG_MAX], line[200];
    u32 ms;
    int lv;

    /* ---- sans port : niveau DEBUG, rien ne sort ---- */
    port_present = 0;
    dlog_init();
    CHECK(!dlog_port_present() && dlog_level() == DL_DEBUG);
    DTRACE("t", "invisible");
    CHECK(dlog_total() == 0);
    DINFO("boot", "salut %d %s", 42, "monde");
    DERROR("sys", "oups %#x", 0xBEEF);
    CHECK(dlog_total() == 2 && dlog_kept() == 2 && port_n == 0);
    CHECK(dlog_get(0, &ms, &lv, tag, msg) && lv == DL_INFO && !strcmp(tag, "boot") && !strcmp(msg, "salut 42 monde") && ms > 1234);
    CHECK(dlog_get(1, &ms, &lv, tag, msg) && lv == DL_ERROR && !strcmp(msg, "oups 0xbeef"));
    CHECK(!dlog_get(2, &ms, &lv, tag, msg));

    /* ---- format ---- */
    dlog_format(line, sizeof line, 12345, DL_WARN, "shell", "texte");
    CHECK(!strcmp(line, "[  12.345 WARN  shell   ] texte"));
    dlog_format(line, sizeof line, 7, DL_TRACE, "x", "m");
    CHECK(!strcmp(line, "[   0.007 TRACE x       ] m"));

    /* ---- niveaux ---- */
    CHECK(dlog_parse_level("trace") == 0 && dlog_parse_level("DEBUG") == 1 && dlog_parse_level("Info") == 2);
    CHECK(dlog_parse_level("warn") == 3 && dlog_parse_level("error") == 4 && dlog_parse_level("3") == 3);
    CHECK(dlog_parse_level("") == -1 && dlog_parse_level("bidon") == -1 && dlog_parse_level("tracer") == -1 && dlog_parse_level("5") == -1);
    CHECK(!strcmp(dlog_level_name(2), "INFO ") && !strcmp(dlog_level_name(9), "?????"));
    dlog_set_level(DL_WARN);
    DINFO("a", "non");
    DWARN("a", "oui");
    CHECK(dlog_total() == 3);
    dlog_set_level(99);
    CHECK(dlog_level() == DL_ERROR);
    dlog_set_level(-5);
    CHECK(dlog_level() == DL_TRACE);
    DTRACE("a", "trace maintenant");
    CHECK(dlog_total() == 4 && port_n == 0);

    /* ---- messages longs, retours a la ligne, etiquettes longues ---- */
    dlog_clear();
    CHECK(dlog_total() == 0 && dlog_kept() == 0);
    {
        char big[300];

        memset(big, 'z', sizeof big - 1);
        big[sizeof big - 1] = 0;
        dlog(DL_INFO, "etiquette-trop-longue", "%s", big);
        CHECK(dlog_get(0, &ms, &lv, tag, msg) && strlen(tag) == 8 && strlen(msg) == DLOG_MSG_MAX - 1 && msg[0] == 'z');
    }
    dlog(DL_INFO, "nl", "a\nb\rc");
    CHECK(dlog_get(1, &ms, &lv, tag, msg) && !strcmp(msg, "a b c"));
    dlog(DL_INFO, NULL, "sans etiquette");
    CHECK(dlog_get(2, &ms, &lv, tag, msg) && tag[0] == 0 && !strcmp(msg, "sans etiquette"));
    dlog(DL_INFO, "p", "100%% %c|%5d|%-4s|", 'x', 7, "ab");
    CHECK(dlog_get(3, &ms, &lv, tag, msg) && !strcmp(msg, "100% x|    7|ab  |"));

    /* ---- anneau : seules les 1024 dernieres lignes restent ---- */
    dlog_clear();
    for (u32 i = 0; i < DLOG_ENTRIES + 50; i++)
        DINFO("ring", "ligne %u", i);
    CHECK(dlog_total() == DLOG_ENTRIES + 50 && dlog_kept() == DLOG_ENTRIES);
    CHECK(dlog_get(0, &ms, &lv, tag, msg) && !strcmp(msg, "ligne 50"));
    CHECK(dlog_get(DLOG_ENTRIES - 1, &ms, &lv, tag, msg) && !strcmp(msg, "ligne 1073"));
    CHECK(!dlog_get(DLOG_ENTRIES, &ms, &lv, tag, msg));

    /* ---- avec port 0xE9 : detecte, TRACE actif, tout sort sur le port, une ligne par message ---- */
    port_present = 1;
    dlog_init();
    CHECK(dlog_port_present() && dlog_level() == DL_TRACE);
    dlog_clear();
    port_n = 0;
    port_out[0] = 0;
    DTRACE("sys", "write(1) = %d", 5);
    DERROR("boot", "fin");
    CHECK(has("TRACE sys     ] write(1) = 5\n") && has("ERROR boot    ] fin\n"));
    CHECK(dlog_total() == 2);
    {
        int lines = 0;

        for (u32 i = 0; i < port_n; i++)
            lines += port_out[i] == '\n';
        CHECK(lines == 2);
    }

    out("  journal de debogage verifie : anneau, niveaux, format, port 0xE9, debordement\n");
    return 0;
}
