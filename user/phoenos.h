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

enum { SYS_EXIT = 1, SYS_READ = 3, SYS_WRITE = 4, SYS_OPEN = 5, SYS_CLOSE = 6, SYS_WAITPID = 7, SYS_UNLINK = 10, SYS_TIME = 13,
       SYS_GETPID = 20, SYS_KILL = 37, SYS_BRK = 45, SYS_GETPPID = 64, SYS_SCHED_YIELD = 158,
       SYS_LOG = 199, SYS_SLEEP_MS = 200, SYS_UPTIME = 201, SYS_SPAWN = 202, SYS_PSL = 203 };

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
/* Journal du systeme (commande `log`) : niveau 0 trace .. 4 erreur. */
static inline int log_msg(int level, const char *text)  { return syscall3(SYS_LOG, level, (int)text, 0); }
static inline void sleep_ms(uint ms)                    { syscall3(SYS_SLEEP_MS, (int)ms, 0, 0); }
static inline uint uptime_ms(void)                      { return (uint)syscall3(SYS_UPTIME, 0, 0, 0); }

/*
 * Programmes qui en lancent d'autres (0.7.0).
 *   spawn(chemin, argv)     lance un programme enfant (argv : tableau de textes termine par 0 ; argv[0] = son nom ;
 *                           0 : seulement le chemin). Renvoie son numero, ou un code d'erreur negatif.
 *   waitpid(pid, &code, 0)  attend la fin de l'enfant `pid` (-1 : n'importe lequel, le premier fini) ; renvoie son numero ;
 *                           *code = son code de sortie. WNOHANG : renvoie 0 s'il tourne encore. E_CHILD (-18) : pas votre enfant.
 *   kill(pid, signal)       arrete le programme `pid` (le signal est ignore). yield() : cede la place aux autres.
 * (spawn et waitpid sont propres a PhoenOS : sous Linux ils echouent, comme tout numero inconnu.)
 */
enum { WNOHANG = 1, E_CHILD = -18 };
static inline int  spawn(const char *path, char *const argv[])  { return syscall3(SYS_SPAWN, (int)path, (int)argv, 0); }
static inline int  waitpid(int pid, int *code, int options)     { return syscall3(SYS_WAITPID, pid, (int)code, options); }
static inline int  kill(int pid, int sig)                       { return syscall3(SYS_KILL, pid, sig, 0); }
static inline int  getppid(void)                                { return syscall3(SYS_GETPPID, 0, 0, 0); }
static inline void yield(void)                                  { syscall3(SYS_SCHED_YIELD, 0, 0, 0); }
/*
 * Bibliotheques partagees (0.8.0, voir man bibliotheques).
 *   psl_sym(lib, sym)   charge la bibliotheque /lib/<lib>.psl si besoin et renvoie l'adresse de la fonction ou variable
 *                       `sym` qu'elle exporte, ou NULL si la bibliotheque ou le symbole n'existe pas ; sym NULL : adresse
 *                       de base. psl_addr donne le meme resultat mais avec le code d'erreur (negatif) en cas d'echec.
 *   Un programme .poe qui declare ses bibliotheques (poepack -l) les a deja : psl_sym ne fait alors que chercher le symbole.
 */
enum { E_NOLIB = -20 };
static inline int psl_addr(const char *lib, const char *sym)    { return syscall3(SYS_PSL, (int)lib, (int)sym, 0); }
static inline void *psl_sym(const char *lib, const char *sym)   { int a = psl_addr(lib, sym); return a > 0 ? (void *)a : (void *)0; }

/* Tas : brk(0) donne la fin du tas, brk(adresse) la deplace et renvoie la nouvelle fin (inchangee si refuse). */
static inline uint brk_to(uint addr)                            { return (uint)syscall3(SYS_BRK, (int)addr, 0, 0); }

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
