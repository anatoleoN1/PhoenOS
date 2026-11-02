#ifndef PHOENOS_TYPES_H
#define PHOENOS_TYPES_H

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef signed int         i32;
typedef unsigned long long u64;
typedef signed long long   i64;
#ifdef __x86_64__
typedef unsigned long      usize;       /* perf64 : taille d'un pointeur */
#else
typedef unsigned int       usize;
#endif

#define NULL ((void *)0)

#endif
