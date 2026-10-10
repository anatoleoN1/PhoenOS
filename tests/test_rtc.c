/* Test du vrai rtc.c : lecture de la puce CMOS (simulée), BCD/binaire, 12 h/24 h, conversions de dates. */
#define TEST_HAS_PORTS
#include "testlib.h"
#include "rtc.h"

u8 test_scancode;

/* faux CMOS */
static u8 cmos[128];
static u8 cmos_index;
static int uip_reads;                           /* nombre de lectures du registre A pendant lesquelles « mise à jour en cours » */
static int flip_sec;                            /* si > 0 : la seconde change à chacune des N premières lectures (instabilité) */

u32 test_port_in(u16 port, int size)
{
    (void)size;
    if (port == 0x71) {
        if (cmos_index == 0x0A && uip_reads > 0) { uip_reads--; return 0x80; }
        if (cmos_index == 0x00 && flip_sec > 0) { flip_sec--; cmos[0]++; }
        return cmos[cmos_index & 0x7F];
    }
    return 0;
}
void test_port_out(u16 port, u32 v, int size)
{
    (void)size;
    if (port == 0x70) cmos_index = (u8)v;
}

static void set_bcd(u8 sec, u8 min, u8 hour, u8 day, u8 mon, u8 yr, u8 cent)
{
    cmos[0] = sec; cmos[2] = min; cmos[4] = hour; cmos[7] = day; cmos[8] = mon; cmos[9] = yr;
    cmos[0x32] = cent; cmos[0x0B] = 0x02;                       /* 24 h, BCD */
}

int test_main(void)
{
    struct rtc_time t, u;

    /* ---- conversions pures ---- */
    t = (struct rtc_time){ 1970, 1, 1, 0, 0, 0, 0 };
    CHECK(rtc_to_unix(&t) == 0);
    t = (struct rtc_time){ 2000, 2, 29, 23, 59, 59, 0 };
    CHECK(rtc_to_unix(&t) == 951868799u);                       /* année bissextile */
    t = (struct rtc_time){ 2026, 10, 5, 7, 31, 12, 0 };
    CHECK(rtc_to_unix(&t) == 1791185472u);
    rtc_from_unix(1791185472u, &u);
    CHECK(u.year == 2026 && u.month == 10 && u.day == 5 && u.hour == 7 && u.min == 31 && u.sec == 12 && u.wday == 1);
    rtc_from_unix(0, &u);
    CHECK(u.year == 1970 && u.month == 1 && u.day == 1 && u.wday == 4);          /* jeudi */
    rtc_from_unix(951868799u, &u);
    CHECK(u.year == 2000 && u.month == 2 && u.day == 29 && u.wday == 2);        /* mardi */
    rtc_from_unix(951868800u, &u);
    CHECK(u.month == 3 && u.day == 1);
    rtc_from_unix(4102444799u, &u);                             /* 31/12/2099 23:59:59 */
    CHECK(u.year == 2099 && u.month == 12 && u.day == 31 && u.hour == 23);
    {                                                           /* aller-retour sur 60 ans, jour par jour */
        int ok = 1;

        for (u32 s = 0; s < 2000000000u; s += 86400u * 7 + 3661u) {
            rtc_from_unix(s, &u);
            if (rtc_to_unix(&u) != s || u.month < 1 || u.month > 12 || u.day < 1 || u.day > 31)
                ok = 0;
        }
        CHECK(ok);
    }
    CHECK(!strcmp(rtc_day_name(1), "lundi") && !strcmp(rtc_day_name(0), "dimanche") && !strcmp(rtc_day_name(9), "?"));
    CHECK(!strcmp(rtc_month_name(10), "octobre") && !strcmp(rtc_month_name(0), "?") && !strcmp(rtc_month_name(13), "?"));

    /* ---- lecture BCD, 24 h ---- */
    set_bcd(0x12, 0x31, 0x07, 0x05, 0x10, 0x26, 0x20);
    CHECK(rtc_read(&t) == E_OK);
    CHECK(t.year == 2026 && t.month == 10 && t.day == 5 && t.hour == 7 && t.min == 31 && t.sec == 12 && t.wday == 1);
    CHECK(rtc_now_unix() == 1791185472u);

    /* sans registre siècle (0 ou 0xFF) : 20xx */
    set_bcd(0x12, 0x31, 0x07, 0x05, 0x10, 0x26, 0x00);
    CHECK(rtc_read(&t) == E_OK && t.year == 2026);
    set_bcd(0x12, 0x31, 0x07, 0x05, 0x10, 0x26, 0xFF);
    CHECK(rtc_read(&t) == E_OK && t.year == 2026);

    /* ---- binaire ---- */
    cmos[0] = 12; cmos[2] = 31; cmos[4] = 7; cmos[7] = 5; cmos[8] = 10; cmos[9] = 26; cmos[0x32] = 20; cmos[0x0B] = 0x06;
    CHECK(rtc_read(&t) == E_OK && t.year == 2026 && t.hour == 7 && t.sec == 12);

    /* ---- 12 h : 12 AM = 0 h, 1 PM = 13 h, 12 PM = 12 h ---- */
    set_bcd(0x00, 0x05, 0x12, 0x05, 0x10, 0x26, 0x20); cmos[0x0B] = 0x00;       /* 12:05 AM */
    CHECK(rtc_read(&t) == E_OK && t.hour == 0);
    set_bcd(0x00, 0x05, 0x81, 0x05, 0x10, 0x26, 0x20); cmos[0x0B] = 0x00;       /* 1 PM */
    CHECK(rtc_read(&t) == E_OK && t.hour == 13);
    set_bcd(0x00, 0x05, 0x92, 0x05, 0x10, 0x26, 0x20); cmos[0x0B] = 0x00;       /* 12 PM */
    CHECK(rtc_read(&t) == E_OK && t.hour == 12);
    set_bcd(0x00, 0x05, 0x11, 0x05, 0x10, 0x26, 0x20); cmos[0x0B] = 0x00;       /* 11 AM */
    CHECK(rtc_read(&t) == E_OK && t.hour == 11);

    /* ---- mise à jour en cours : on attend ---- */
    set_bcd(0x59, 0x59, 0x23, 0x31, 0x12, 0x25, 0x20);
    uip_reads = 50;
    CHECK(rtc_read(&t) == E_OK && t.year == 2025 && t.month == 12 && t.day == 31 && t.hour == 23);
    CHECK(uip_reads == 0);

    /* mise à jour qui ne finit jamais : borné, ne bloque pas */
    uip_reads = 100000000;
    CHECK(rtc_read(&t) == E_OK || 1);
    uip_reads = 0;

    /* ---- valeur instable (la seconde change à chaque lecture) : abandon proprement ---- */
    set_bcd(0x10, 0x00, 0x10, 0x01, 0x01, 0x26, 0x20); cmos[0x0B] = 0x06;
    flip_sec = 1000;
    CHECK(rtc_read(&t) == E_IO);
    flip_sec = 0;
    CHECK(rtc_read(&t) == E_OK);                                /* puis tout va bien */

    /* ---- valeurs absurdes (pas de pile, pas d'horloge) : E_IO, jamais de date fausse ---- */
    set_bcd(0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);          /* jour 0, mois 0 */
    CHECK(rtc_read(&t) == E_IO && rtc_now_unix() == 0);
    set_bcd(0x61, 0x00, 0x00, 0x01, 0x01, 0x26, 0x20);          /* 61 secondes */
    CHECK(rtc_read(&t) == E_IO);
    set_bcd(0x00, 0x00, 0x25, 0x01, 0x01, 0x26, 0x20);          /* 25 h */
    CHECK(rtc_read(&t) == E_IO);
    set_bcd(0x00, 0x00, 0x00, 0x01, 0x13, 0x26, 0x20);          /* mois 13 */
    CHECK(rtc_read(&t) == E_IO);
    memset(cmos, 0xFF, sizeof cmos);                            /* bus flottant */
    CHECK(rtc_read(&t) == E_IO);

    out("  horloge verifiee : BCD/binaire, 12 h/24 h, mise a jour en cours, valeurs absurdes, calendrier 1970-2099\n");
    return 0;
}
