#ifndef PHOENOS_STRING_H
#define PHOENOS_STRING_H

#include "types.h"

void *memset(void *dst, int value, usize n);
void *memcpy(void *dst, const void *src, usize n);
void *memmove(void *dst, const void *src, usize n);
int   memcmp(const void *a, const void *b, usize n);
char *strchr(const char *s, int c);
char *strcpy(char *dst, const char *src);
usize strlen(const char *s);
int   strcmp(const char *a, const char *b);

#endif
