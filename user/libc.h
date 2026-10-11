/*
 * PhoenOS - libc.h : mini-bibliotheque C des programmes (0.7.0), a inclure EN PLUS de phoenos.h.
 *
 * Tout est "static" : un programme n'embarque que les fonctions qu'il utilise (pas de .o a lier).
 * Contenu : memoire (memcpy, memset...), textes (strlen, strcmp...), TAS (malloc, free, calloc, realloc),
 * affichage formate (printf, snprintf), tri (qsort), texte des codes d'erreur (err_text).
 * Comme phoenos.h, ce fichier marche aussi sous Linux 32 bits : c'est comme ca que les tests le verifient.
 */
#ifndef PHOENOS_LIBC_H
#define PHOENOS_LIBC_H

#include "phoenos.h"
#include <stdarg.h>

#define NULL ((void *)0)

/* ---- memoire ---- */
static inline void *memcpy(void *d, const void *s, uint n)
{
    uchar *a = d;
    const uchar *b = s;

    while (n--)
        *a++ = *b++;
    return d;
}

static inline void *memmove(void *d, const void *s, uint n)
{
    uchar *a = d;
    const uchar *b = s;

    if (a < b)
        while (n--)
            *a++ = *b++;
    else
        while (n--)
            a[n] = b[n];
    return d;
}

static inline void *memset(void *d, int c, uint n)
{
    uchar *a = d;

    while (n--)
        *a++ = (uchar)c;
    return d;
}

static inline int memcmp(const void *x, const void *y, uint n)
{
    const uchar *a = x, *b = y;

    for (; n; n--, a++, b++)
        if (*a != *b)
            return *a - *b;
    return 0;
}

/* ---- textes ---- */
static inline uint strlen(const char *s) { return slen(s); }
static inline int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (uchar)*a - (uchar)*b;
}
static inline int strncmp(const char *a, const char *b, uint n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b)
            return (uchar)*a - (uchar)*b;
        if (!*a)
            return 0;
    }
    return 0;
}
static inline char *strcpy(char *d, const char *s)
{
    char *r = d;

    while ((*d++ = *s++))
        ;
    return r;
}
static inline char *strchr(const char *s, int c)
{
    for (; *s; s++)
        if (*s == (char)c)
            return (char *)s;
    return c == 0 ? (char *)s : NULL;
}

/* ---- le tas : malloc / free / calloc / realloc ----
 *
 * Le noyau ne donne qu'une chose : brk(), qui deplace la fin d'une zone continue (le tas). Cet
 * allocateur la decoupe en BLOCS, chacun precede d'un en-tete (taille, libre ou non, voisins) :
 *   [en-tete|donnees...][en-tete|donnees...][en-tete|donnees...]   <- fin du tas (brk)
 * malloc prend le premier bloc libre assez grand (et le coupe s'il est trop gros) ; sinon il demande
 * de la place au noyau. free rend le bloc et le FUSIONNE avec ses voisins libres ; si la fin du tas est
 * libre sur plus de 32 Kio, la place est rendue au noyau. Un en-tete abime ou un pointeur invalide
 * arrete le programme avec un message (comme le noyau le fait pour son propre tas).
 */
#define HEAP_MAGIC_USED 0xA110C8EDu
#define HEAP_MAGIC_FREE 0xF4EE0001u
#define HEAP_GROW       16384u
#define HEAP_GIVE_BACK  32768u

struct heap_blk {
    uint size;                                  /* octets de donnees (multiple de 16) */
    uint magic;
    struct heap_blk *prev, *next;               /* voisins en memoire (adresses croissantes) */
};
#define HEAP_HDR ((uint)sizeof(struct heap_blk))   /* 16 : les donnees restent alignees sur 16 */

static struct heap_blk *heap_head, *heap_tail;
static uint heap_total;                         /* octets pris au noyau (en-tetes compris) */

static inline void heap_die(const char *msg)
{
    puts_fd(2, msg);
    puts_fd(2, "\n");
    exit(134);
}

/* Demande `bytes` octets au noyau (multiple de 4096) ; ajoute un bloc libre a la fin. 0 : refuse. */
static inline int heap_grow(uint bytes)
{
    uint cur = brk_to(0), want, got;
    struct heap_blk *b;

    if (cur & 0xFFFu) {                         /* debut du tas non aligne (jamais sous PhoenOS) : on s'aligne */
        uint al = (cur + 0xFFFu) & ~0xFFFu;

        if (brk_to(al) != al)
            return 0;
        cur = al;
    }
    want = cur + bytes;
    got = brk_to(want);
    if (got != want)
        return 0;                               /* refuse : plus de memoire, ou limite du tas atteinte */
    b = (struct heap_blk *)cur;
    b->size = bytes - HEAP_HDR;
    b->magic = HEAP_MAGIC_FREE;
    b->prev = heap_tail;
    b->next = NULL;
    if (heap_tail)
        heap_tail->next = b;
    else
        heap_head = b;
    heap_tail = b;
    heap_total += bytes;
    /* fusion avec le bloc libre precedent s'il touche celui-ci */
    if (b->prev && b->prev->magic == HEAP_MAGIC_FREE && (uchar *)b->prev + HEAP_HDR + b->prev->size == (uchar *)b) {
        b->prev->size += HEAP_HDR + b->size;
        b->prev->next = NULL;
        heap_tail = b->prev;
    }
    return 1;
}

static inline void *malloc(uint n)
{
    struct heap_blk *b;

    if (n == 0)
        return NULL;
    if (n > 0x40000000u)
        return NULL;
    n = (n + 15u) & ~15u;
    for (;;) {
        for (b = heap_head; b; b = b->next) {
            if (b->magic != HEAP_MAGIC_USED && b->magic != HEAP_MAGIC_FREE)
                heap_die("malloc: tas corrompu");
            if (b->magic == HEAP_MAGIC_FREE && b->size >= n)
                break;
        }
        if (b)
            break;
        {
            uint need = n + HEAP_HDR, g = need > HEAP_GROW ? need : HEAP_GROW;

            if (!heap_grow((g + 0xFFFu) & ~0xFFFu))
                return NULL;
        }
    }
    if (b->size >= n + HEAP_HDR + 16) {         /* assez de reste : on coupe */
        struct heap_blk *r = (struct heap_blk *)((uchar *)b + HEAP_HDR + n);

        r->size = b->size - n - HEAP_HDR;
        r->magic = HEAP_MAGIC_FREE;
        r->prev = b;
        r->next = b->next;
        if (b->next)
            b->next->prev = r;
        else
            heap_tail = r;
        b->next = r;
        b->size = n;
    }
    b->magic = HEAP_MAGIC_USED;
    return (uchar *)b + HEAP_HDR;
}

static inline void free(void *p)
{
    struct heap_blk *b;

    if (!p)
        return;
    if ((uint)p & 15u || (uchar *)p < (uchar *)heap_head + HEAP_HDR || (uint)p >= brk_to(0))
        heap_die("free: pointeur invalide");
    b = (struct heap_blk *)((uchar *)p - HEAP_HDR);
    if (b->magic == HEAP_MAGIC_FREE)
        heap_die("free: double liberation");
    if (b->magic != HEAP_MAGIC_USED)
        heap_die("free: pointeur invalide ou tas corrompu");
    b->magic = HEAP_MAGIC_FREE;
    if (b->next && b->next->magic == HEAP_MAGIC_FREE) {                 /* fusion avec le suivant */
        b->size += HEAP_HDR + b->next->size;
        b->next = b->next->next;
        if (b->next)
            b->next->prev = b;
        else
            heap_tail = b;
    }
    if (b->prev && b->prev->magic == HEAP_MAGIC_FREE) {                 /* fusion avec le precedent */
        b->prev->size += HEAP_HDR + b->size;
        b->prev->next = b->next;
        if (b->next)
            b->next->prev = b->prev;
        else
            heap_tail = b->prev;
        b = b->prev;
    }
    if (b == heap_tail && b->size + HEAP_HDR >= HEAP_GIVE_BACK) {       /* la fin du tas est libre : on la rend au noyau */
        uint total = b->size + HEAP_HDR, give = (total - 4096u) & ~0xFFFu;   /* on garde une page, le bloc reste valide */
        uint end = (uint)b + total;

        if (give && brk_to(end - give) == end - give) {
            b->size -= give;
            heap_total -= give;
        }
    }
}

static inline void *calloc(uint count, uint size)
{
    uint total;
    void *p;

    if (size && count > 0x40000000u / size)
        return NULL;
    total = count * size;
    p = malloc(total);
    if (p)
        memset(p, 0, total);
    return p;
}

static inline void *realloc(void *p, uint n)
{
    struct heap_blk *b;
    void *q;

    if (!p)
        return malloc(n);
    if (n == 0) {
        free(p);
        return NULL;
    }
    b = (struct heap_blk *)((uchar *)p - HEAP_HDR);
    if (b->magic != HEAP_MAGIC_USED)
        heap_die("realloc: pointeur invalide");
    if (b->size >= n)
        return p;
    q = malloc(n);
    if (!q)
        return NULL;                            /* l'ancien bloc reste valide */
    memcpy(q, p, b->size);
    free(p);
    return q;
}

/* Octets que le tas a pris au noyau (pour les statistiques) et octets de donnees reellement alloues. */
static inline uint heap_bytes_from_kernel(void) { return heap_total; }
static inline uint heap_bytes_in_use(void)
{
    uint n = 0;

    for (struct heap_blk *b = heap_head; b; b = b->next)
        if (b->magic == HEAP_MAGIC_USED)
            n += b->size;
    return n;
}

/* ---- affichage formate ----
 * snprintf / printf : %d %i %u %x %X %c %s %p %% ; largeur, 0 devant, - (a gauche), l ignore.
 * Pas de virgule flottante. Le texte est toujours termine par 0 dans snprintf. */
static inline int vsnprintf(char *out, uint cap, const char *fmt, va_list ap)
{
    uint n = 0;

#define PUT(ch) do { if (n + 1 < cap) out[n] = (ch); n++; } while (0)
    for (; *fmt; fmt++) {
        char tmp[16], pad = ' ';
        int left = 0, width = 0, len = 0, neg = 0;
        uint v, base = 10;
        const char *s;

        if (*fmt != '%') { PUT(*fmt); continue; }
        fmt++;
        for (;; fmt++) {
            if (*fmt == '-') left = 1;
            else if (*fmt == '0') pad = '0';
            else break;
        }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l')
            fmt++;
        switch (*fmt) {
        case 'd': case 'i': {
            int x = va_arg(ap, int);

            neg = x < 0;
            v = neg ? 0u - (uint)x : (uint)x;
            goto number;
        }
        case 'u': v = va_arg(ap, uint); goto number;
        case 'x': case 'X': case 'p':
            v = va_arg(ap, uint);
            base = 16;
        number:
            do {
                uint d = v % base;

                tmp[len++] = (char)(d < 10 ? '0' + d : (*fmt == 'X' ? 'A' : 'a') + d - 10);
                v /= base;
            } while (v);
            if (*fmt == 'p') { PUT('0'); PUT('x'); }
            if (neg && pad == '0') { PUT('-'); neg = 0; width--; }
            if (neg) len++, tmp[len - 1] = '-';
            if (!left)
                for (; width > len; width--) PUT(pad);
            while (len) { PUT(tmp[--len]); width--; }
            for (; width > 0; width--) PUT(' ');
            break;
        case 'c':
            PUT((char)va_arg(ap, int));
            break;
        case 's':
            s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            len = (int)slen(s);
            if (!left)
                for (; width > len; width--) PUT(' ');
            for (int i = 0; i < len; i++) PUT(s[i]);
            for (width -= len; width > 0; width--) PUT(' ');
            break;
        case '%': PUT('%'); break;
        case 0: fmt--; break;
        default: PUT('%'); PUT(*fmt); break;
        }
    }
#undef PUT
    if (cap)
        out[n < cap ? n : cap - 1] = 0;
    return (int)n;
}

static inline int snprintf(char *out, uint cap, const char *fmt, ...)
{
    va_list ap;
    int r;

    va_start(ap, fmt);
    r = vsnprintf(out, cap, fmt, ap);
    va_end(ap);
    return r;
}

static inline int printf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    int r;

    va_start(ap, fmt);
    r = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    write(1, buf, (uint)(r < (int)sizeof buf ? r : (int)sizeof buf - 1));
    return r;
}

/* ---- tri : qsort(tableau, nombre, taille d'un element, comparaison) ---- */
static inline void qsort(void *base, uint n, uint size, int (*cmp)(const void *, const void *))
{
    uchar *a = base, tmp;
    uint gap = n;
    int swapped = 1;

    while (gap > 1 || swapped) {                /* tri a peignes : simple, sans pile, sans allocation */
        gap = gap * 10 / 13;
        if (gap == 9 || gap == 10)
            gap = 11;
        if (gap < 1)
            gap = 1;
        swapped = 0;
        for (uint i = 0; i + gap < n; i++) {
            uchar *x = a + i * size, *y = a + (i + gap) * size;

            if (cmp(x, y) > 0) {
                for (uint k = 0; k < size; k++) { tmp = x[k]; x[k] = y[k]; y[k] = tmp; }
                swapped = 1;
            }
        }
    }
}

/* Texte d'un code d'erreur renvoye par un appel systeme (spawn, waitpid, open...). */
static inline const char *err_text(int e)
{
    switch (e) {
    case -1:  return "memoire insuffisante";
    case -2:  return "parametre invalide";
    case -3:  return "introuvable";
    case -5:  return "trop de programmes en meme temps";
    case -7:  return "non disponible";
    case -8:  return "trop d'arguments";
    case -12: return "est un dossier";
    case -16: return "pas un programme PhoenOS";
    case -17: return "adresse interdite";
    case -18: return "pas un de vos enfants";
    case -19: return "attente interrompue";
    case -20: return "bibliotheque absente ou invalide";
    default:  return "erreur";
    }
}

#endif
