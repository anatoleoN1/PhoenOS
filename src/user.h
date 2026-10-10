#ifndef PHOENOS_USER_H
#define PHOENOS_USER_H

#include "types.h"
#include "error.h"
#include "isr.h"

struct fs_node;

/*
 * Programmes utilisateur (ring 3). Un seul a la fois. Ils sont charges a USER_BASE dans une
 * zone de 4 Mio reservee au demarrage ; pile en haut de la zone. Pas encore de pagination :
 * la zone n'est PAS isolee du noyau (c'est l'etape suivante), mais les instructions privilegiees,
 * les ports d'entree/sortie et les erreurs sont bien interceptes.
 */
#define USER_BASE       0x400000u
#define USER_SIZE       0x400000u
#define USER_END        (USER_BASE + USER_SIZE)
#define USER_STACK_SIZE 0x10000u
#define USER_MAX_FDS    8
#define USER_FIRST_FD   3               /* 0 entree, 1 sortie, 2 erreurs */
#define USER_ARGS_MAX   16
#define USER_ARGS_BYTES 4096

/* Numeros d'appels systeme : ceux de Linux i386 pour les appels communs (eax, ebx, ecx, edx). */
enum {
    SYS_EXIT     = 1,    /* exit(code)                       */
    SYS_READ     = 3,    /* read(fd, tampon, taille)         */
    SYS_WRITE    = 4,    /* write(fd, tampon, taille)        */
    SYS_OPEN     = 5,    /* open(chemin, 0)  lecture seule   */
    SYS_CLOSE    = 6,    /* close(fd)                        */
    SYS_TIME     = 13,   /* time(&t) : secondes depuis 1970  */
    SYS_GETPID   = 20,   /* getpid()                         */
    SYS_SLEEP_MS = 200,  /* sleep_ms(ms)    propre a PhoenOS */
    SYS_UPTIME   = 201   /* uptime_ms()     propre a PhoenOS */
};

struct user_fd {
    struct fs_node *node;               /* NULL = libre */
    u32 pos;
};

struct uproc {
    int  running;
    int  exit_requested;                /* mis par SYS_EXIT / Ctrl+C : l'appelant doit rendre la main */
    int  exit_code;
    u32  pid;
    char name[32];
    u32  win_lo, win_hi;                /* seule zone que le programme peut passer au noyau */
    struct user_fd fd[USER_MAX_FDS];
    const u8 *in;                       /* entree standard issue d'un tube (sinon in_set == 0) */
    u32  in_len, in_pos;
    int  in_set;
    char line[256];                     /* ligne en cours de lecture au clavier */
    u32  line_len, line_pos;
    struct { u32 ebx, esi, edi, ebp, esp; } ctx;   /* contexte du noyau a retrouver a la fin */
};

/* Noyau : reserve la zone. E_BUSY / E_NOMEM : les programmes seront indisponibles. */
err_t user_init(void);
int   user_available(void);

/* Entree standard du prochain programme : sortie du tube precedent (set = 0 : clavier). */
void user_set_stdin(const u8 *buf, u32 len, int set);

struct user_result {
    int exit_code;                      /* code de exit(), ou 128 + numero d'exception si arrete */
    int killed;                         /* 1 : arrete par le noyau (faute) ou Ctrl+C */
};

/*
 * Charge et execute `image` (fichier ELF) ; rend la main a la fin du programme.
 * E_NOSYS : indisponible ; E_NOEXEC : ELF invalide ; E_BUSY : deja un programme ; E_2BIG : arguments.
 */
err_t user_exec(const u8 *image, u32 size, const char *name, int argc, char **argv,
                struct user_result *res);

/* Appele par isr.c */
void syscall_entry(struct regs *r);
void user_fault(struct regs *r, const char *what);

/* Coeur des appels systeme (pur : testable). Les erreurs sont des E_* negatifs. */
i32 syscall_dispatch(struct uproc *p, u32 nr, u32 a, u32 b, u32 c);
void user_proc_reset(struct uproc *p, const char *name);
struct uproc *user_current(void);

#endif
