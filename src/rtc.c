/*
 * PhoenOS - rtc.c
 * Lecture de l'horloge temps réel (puce CMOS : ports 0x70 = index, 0x71 = donnée).
 */
#include "rtc.h"
#include "io.h"

#define CMOS_INDEX 0x70
#define CMOS_DATA  0x71

#define REG_SEC    0x00
#define REG_MIN    0x02
#define REG_HOUR   0x04
#define REG_DAY    0x07
#define REG_MONTH  0x08
#define REG_YEAR   0x09
#define REG_STA    0x0A      /* bit 7 : mise à jour en cours */
#define REG_STB    0x0B      /* bit 1 : 24 h ; bit 2 : valeurs binaires (sinon BCD) */
#define REG_CENT   0x32      /* siècle (présent sur la plupart des PC, pas tous) */

static u8 cmos_read(u8 reg)
{
    outb(CMOS_INDEX, reg);
    return inb(CMOS_DATA);
}

struct raw {
    u8 sec, min, hour, day, month, year, cent, stb;
};

static void read_raw(struct raw *r)
{
    /* attend la fin d'une éventuelle mise à jour (au plus ~ 2 ms) ; borne pour ne jamais bloquer */
    for (int i = 0; i < 100000 && (cmos_read(REG_STA) & 0x80); i++)
        ;
    r->sec = cmos_read(REG_SEC);
    r->min = cmos_read(REG_MIN);
    r->hour = cmos_read(REG_HOUR);
    r->day = cmos_read(REG_DAY);
    r->month = cmos_read(REG_MONTH);
    r->year = cmos_read(REG_YEAR);
    r->cent = cmos_read(REG_CENT);
    r->stb = cmos_read(REG_STB);
}

static int same(const struct raw *a, const struct raw *b)
{
    return a->sec == b->sec && a->min == b->min && a->hour == b->hour && a->day == b->day &&
           a->month == b->month && a->year == b->year && a->cent == b->cent;
}

static u8 bcd(u8 v)
{
    return (u8)((v & 0x0F) + (v >> 4) * 10);
}

/* ---------------- conversions de dates (algorithme de H. Hinnant, calendrier grégorien) ---------------- */

static i32 days_from_civil(i32 y, u32 m, u32 d)
{
    i32 era;
    u32 yoe, doy, doe;

    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (u32)(y - era * 400);
    doy = (153 * (m + (m > 2 ? (u32)-3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (i32)doe - 719468;
}

static void civil_from_days(i32 z, u16 *y, u8 *m, u8 *d)
{
    i32 era;
    u32 doe, yoe, doy, mp, dd, mm;
    i32 yy;

    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = (u32)(z - era * 146097);
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    yy = (i32)yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    dd = doy - (153 * mp + 2) / 5 + 1;
    mm = mp < 10 ? mp + 3 : mp - 9;
    *y = (u16)(yy + (mm <= 2));
    *m = (u8)mm;
    *d = (u8)dd;
}

u32 rtc_to_unix(const struct rtc_time *t)
{
    i32 days = days_from_civil(t->year, t->month, t->day);

    if (days < 0)
        return 0;
    return (u32)days * 86400u + (u32)t->hour * 3600u + (u32)t->min * 60u + t->sec;
}

void rtc_from_unix(u32 secs, struct rtc_time *t)
{
    u32 days = secs / 86400u, rem = secs % 86400u;

    civil_from_days((i32)days, &t->year, &t->month, &t->day);
    t->hour = (u8)(rem / 3600);
    t->min = (u8)(rem % 3600 / 60);
    t->sec = (u8)(rem % 60);
    t->wday = (u8)((days + 4) % 7);             /* le 1er janvier 1970 était un jeudi */
}

err_t rtc_read(struct rtc_time *t)
{
    struct raw a, b;
    u8 hour, pm;
    int binary, h24;
    unsigned year;

    /* deux lectures identiques de suite : évite de lire pendant un changement de seconde */
    int stable = 0;

    read_raw(&a);
    for (int i = 0; i < 5 && !stable; i++) {
        read_raw(&b);
        stable = same(&a, &b);
        a = b;
    }
    if (!stable)
        return E_IO;

    binary = (a.stb & 0x04) != 0;
    h24 = (a.stb & 0x02) != 0;
    pm = a.hour & 0x80;
    hour = a.hour & 0x7F;
    if (!binary) {
        a.sec = bcd(a.sec); a.min = bcd(a.min); hour = bcd(hour);
        a.day = bcd(a.day); a.month = bcd(a.month); a.year = bcd(a.year);
        if (a.cent != 0xFF)
            a.cent = bcd(a.cent);
    }
    if (!h24) {                                 /* 12 h : 12 AM = 0 h, 12 PM = 12 h */
        hour %= 12;
        if (pm)
            hour += 12;
    }

    year = a.year;
    year += (a.cent >= 19 && a.cent <= 21) ? (unsigned)a.cent * 100 : 2000;

    if (a.month < 1 || a.month > 12 || a.day < 1 || a.day > 31 || hour > 23 || a.min > 59 || a.sec > 59)
        return E_IO;
    t->year = (u16)year;
    t->month = a.month;
    t->day = a.day;
    t->hour = hour;
    t->min = a.min;
    t->sec = a.sec;
    t->wday = (u8)((days_from_civil((i32)year, a.month, a.day) + 4) % 7);
    if (days_from_civil((i32)year, a.month, a.day) < 0)
        return E_IO;
    return E_OK;
}

u32 rtc_now_unix(void)
{
    struct rtc_time t;

    return rtc_read(&t) == E_OK ? rtc_to_unix(&t) : 0;
}

const char *rtc_day_name(int wday)
{
    static const char *const n[7] = { "dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi" };

    return (wday >= 0 && wday < 7) ? n[wday] : "?";
}

const char *rtc_month_name(int month)
{
    static const char *const n[12] = { "janvier", "fevrier", "mars", "avril", "mai", "juin", "juillet",
                                       "aout", "septembre", "octobre", "novembre", "decembre" };

    return (month >= 1 && month <= 12) ? n[month - 1] : "?";
}
