#ifndef PHOENOS_VERSION_H
#define PHOENOS_VERSION_H

#define PHOENOS_NAME    "PhoenOS"
#define PHOENOS_VERSION "0.11.3"

/* Numero de build = x*1000000 + y*1000 + z : sert a comparer les versions (mises a jour, anti-retour-arriere).
 * Un test (tests/run.sh) verifie qu'il correspond a PHOENOS_VERSION. */
#define PHOENOS_BUILD   11003u

/* Edition de ce noyau : perf32 (Performance 32 bits), perf64 (Performance 64 bits), pui64 (Puissance 64 bits). */
#ifdef PHOENOS_ED_PERF64
#define PHOENOS_ARCH_TEXT  "x86-64 (64 bits)"
#define PHOENOS_EDITION    "perf64"
#define PHOENOS_EDITION_ID 2u
#else
#define PHOENOS_ARCH_TEXT  "x86 (32 bits)"
#define PHOENOS_EDITION    "perf32"
#define PHOENOS_EDITION_ID 1u
#endif

/* Tests des mises a jour seulement (-DPHOENOS_TEST_PIN_OLD) : les manifestes signes des vecteurs de test supposent
 * un noyau "0.10.4" ; ainsi les tests ne dependent pas du numero de version courant. */
#ifdef PHOENOS_TEST_PIN_OLD
#undef PHOENOS_VERSION
#undef PHOENOS_BUILD
#define PIN_OLD_VERSION "0.10.4"
#define PHOENOS_VERSION PIN_OLD_VERSION
#define PHOENOS_BUILD   10004u
#endif

#endif
