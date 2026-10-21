#ifndef PHOENOS_VERSION_H
#define PHOENOS_VERSION_H

#define PHOENOS_NAME    "PhoenOS"
#define PHOENOS_VERSION "0.10.4"

/* Numero de build = x*1000000 + y*1000 + z : sert a comparer les versions (mises a jour, anti-retour-arriere).
 * Un test (tests/run.sh) verifie qu'il correspond a PHOENOS_VERSION. */
#define PHOENOS_BUILD   10004u

/* Edition de ce noyau : perf32 (Performance 32 bits), perf64 (Performance 64 bits), pui64 (Puissance 64 bits). */
#define PHOENOS_EDITION    "perf32"
#define PHOENOS_EDITION_ID 1u

#endif
