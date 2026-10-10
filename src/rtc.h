#ifndef PHOENOS_RTC_H
#define PHOENOS_RTC_H

#include "types.h"
#include "error.h"

/*
 * Horloge matérielle (RTC / CMOS) : date et heure, à la seconde près.
 * Ce que l'horloge indique est pris tel quel (PhoenOS ne gère pas les fuseaux horaires) :
 * sous QEMU, `make run` démarre avec l'heure locale de votre machine.
 */
struct rtc_time {
    u16 year;       /* 2000..2099 */
    u8  month;      /* 1..12      */
    u8  day;        /* 1..31      */
    u8  hour, min, sec;
    u8  wday;       /* 0 = dimanche ... 6 = samedi (calculé) */
};

/* Lit l'horloge. E_IO si les valeurs lues sont absurdes (pas d'horloge, pile vide...). */
err_t rtc_read(struct rtc_time *t);

/* Secondes écoulées depuis le 1er janvier 1970 00:00 (de l'heure lue, sans fuseau), ou 0 si l'horloge est illisible. */
u32 rtc_now_unix(void);

/* Conversions pures (testables) : date <-> secondes depuis 1970. */
u32  rtc_to_unix(const struct rtc_time *t);
void rtc_from_unix(u32 secs, struct rtc_time *t);

/* Noms en français : jour 0..6 (dimanche = 0), mois 1..12. */
const char *rtc_day_name(int wday);
const char *rtc_month_name(int month);

#endif
