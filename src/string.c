/*
 * PhoenOS - string.c
 * Fonctions de base de la libc, nécessaires au noyau (le compilateur peut
 * aussi en générer des appels). Compilé avec -fno-tree-loop-distribute-patterns
 * pour que ces boucles ne soient pas remplacées par des appels à elles-mêmes.
 */
#include "string.h"

void *memset(void *dst, int value, usize n)
{
    u8 *d = dst;

    while (n--)
        *d++ = (u8)value;
    return dst;
}

void *memcpy(void *dst, const void *src, usize n)
{
    u8 *d = dst;
    const u8 *s = src;

    while (n--)
        *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, usize n)
{
    u8 *d = dst;
    const u8 *s = src;

    if (d == s || n == 0)
        return dst;

    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}

int memcmp(const void *a, const void *b, usize n)
{
    const u8 *x = a;
    const u8 *y = b;

    for (; n; n--, x++, y++)
        if (*x != *y)
            return *x - *y;
    return 0;
}

usize strlen(const char *s)
{
    usize n = 0;

    while (s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (u8)*a - (u8)*b;
}

char *strchr(const char *s, int c)
{
    for (; *s; s++)
        if (*s == (char)c)
            return (char *)s;
    return c == 0 ? (char *)s : NULL;
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;

    while ((*d++ = *src++))
        ;
    return dst;
}
