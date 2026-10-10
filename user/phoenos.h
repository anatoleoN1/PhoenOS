/*
 * PhoenOS - phoenos.h : mini-bibliotheque des programmes utilisateur (sans libc).
 * Appels systeme par int 0x80 : numero dans eax, arguments dans ebx, ecx, edx.
 * Les numeros communs (exit, read, write, open, close, time, getpid) sont ceux de Linux i386 :
 * ces programmes tournent donc aussi sous Linux 32 bits, ce que font les tests automatiques.
 */
#ifndef PHOENOS_USER_LIB_H
#define PHOENOS_USER_LIB_H

typedef unsigned int  uint;
typedef unsigned char uchar;

enum { SYS_EXIT = 1, SYS_READ = 3, SYS_WRITE = 4, SYS_OPEN = 5, SYS_CLOSE = 6, SYS_UNLINK = 10, SYS_TIME = 13,
       SYS_GETPID = 20, SYS_SLEEP_MS = 200, SYS_UPTIME = 201 };

static inline int syscall3(int n, int a, int b, int c)
{
    int r;

    __asm__ volatile ("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return r;
}

static inline void exit(int code) __attribute__((noreturn));
static inline void exit(int code) { syscall3(SYS_EXIT, code, 0, 0); for (;;) ; }
static inline int  write(int fd, const void *b, uint n) { return syscall3(SYS_WRITE, fd, (int)b, (int)n); }
static inline int  read(int fd, void *b, uint n)        { return syscall3(SYS_READ, fd, (int)b, (int)n); }
/* Options d'open (comme Linux) : ecrire = O_WRONLY ou O_RDWR ; les ecritures vont toujours en fin de fichier. */
enum { O_RDONLY = 0, O_WRONLY = 1, O_RDWR = 2, O_CREAT = 0x40, O_TRUNC = 0x200, O_APPEND = 0x400 };
static inline int  open_flags(const char *path, int flags) { return syscall3(SYS_OPEN, (int)path, flags, 0644); }   /* 0644 : droits si Linux cree le fichier (ignore par PhoenOS) */
static inline int  open(const char *path)               { return syscall3(SYS_OPEN, (int)path, 0, 0); }
static inline int  unlink(const char *path)             { return syscall3(SYS_UNLINK, (int)path, 0, 0); }
static inline int  close(int fd)                        { return syscall3(SYS_CLOSE, fd, 0, 0); }
static inline int  getpid(void)                         { return syscall3(SYS_GETPID, 0, 0, 0); }
static inline uint time(void)                           { return (uint)syscall3(SYS_TIME, 0, 0, 0); }
static inline void sleep_ms(uint ms)                    { syscall3(SYS_SLEEP_MS, (int)ms, 0, 0); }
static inline uint uptime_ms(void)                      { return (uint)syscall3(SYS_UPTIME, 0, 0, 0); }

static inline uint slen(const char *s) { uint n = 0; while (s[n]) n++; return n; }
static inline void puts_fd(int fd, const char *s) { write(fd, s, slen(s)); }
static inline void puts(const char *s) { puts_fd(1, s); }

static inline void putu(uint v)
{
    char b[12];
    int i = 11;

    b[i] = 0;
    do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v);
    puts(b + i);
}

/* Entier decimal (espaces et signe ignores s'il n'y a pas de chiffre : renvoie 0). */
static inline int atoi(const char *s)
{
    int v = 0;

    while (*s >= '0' && *s <= '9')
        v = v * 10 + (*s++ - '0');
    return v;
}

static inline int streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

#endif
