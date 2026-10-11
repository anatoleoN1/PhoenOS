#ifndef PHOENOS_USER_H
#define PHOENOS_USER_H

#include "types.h"
#include "error.h"
#include "isr.h"
#include "poe.h"

struct fs_node;
struct aspace;

/*
 * Programmes utilisateur (ring 3). Plusieurs a la fois (0.6.0) : chacun est une tache de
 * l'ordonnanceur (sched.h) avec son espace d'adresses (aspace.h), vu a USER_BASE..USER_END ;
 * pile en haut de la zone. Le programme "de premier plan" bloque le shell jusqu'a sa fin ;
 * un programme lance avec & tourne en arriere-plan (sans clavier : sa lecture donne la fin de fichier).
 */
#define USER_BASE       0x400000u
#define USER_SIZE       0x400000u
#define USER_END        (USER_BASE + USER_SIZE)
#define USER_STACK_SIZE 0x10000u
#define USER_MAX_FDS    8
#define USER_FIRST_FD   3               /* 0 entree, 1 sortie, 2 erreurs */
#define USER_ARGS_MAX   16
#define USER_ARGS_BYTES 4096
#define USER_HEAP_GUARD 0x1000u         /* page jamais mappee entre le tas et la pile : un debordement fait une faute */
#define WNOHANG         1
#define PSL_PER_PROC    8               /* bibliotheques .psl utilisables en meme temps par un programme */

/* Numeros d'appels systeme : ceux de Linux i386 pour les appels communs (eax, ebx, ecx, edx). */
enum {
    SYS_EXIT     = 1,    /* exit(code)                       */
    SYS_READ     = 3,    /* read(fd, tampon, taille)         */
    SYS_WRITE    = 4,    /* write(fd, tampon, taille)        */
    SYS_OPEN     = 5,    /* open(chemin, options) : O_WRONLY 1, O_RDWR 2, O_CREAT 0x40, O_TRUNC 0x200, O_APPEND 0x400 */
    SYS_CLOSE    = 6,    /* close(fd)                        */
    SYS_WAITPID  = 7,    /* waitpid(pid, &code, options) : attend la fin d'un programme enfant (pid -1 : n'importe lequel ; options 1 = WNOHANG) */
    SYS_UNLINK   = 10,   /* unlink(chemin) : supprime un fichier */
    SYS_TIME     = 13,   /* time(&t) : secondes depuis 1970  */
    SYS_GETPID   = 20,   /* getpid()                         */
    SYS_KILL     = 37,   /* kill(pid, signal) : demande l'arret d'un programme (le signal est ignore : le programme est toujours arrete) */
    SYS_BRK      = 45,   /* brk(adresse) : fin du tas ; 0 = la lire ; renvoie la fin du tas (inchangee si refuse) */
    SYS_GETPPID  = 64,   /* getppid() : numero du programme parent (0 : le shell) */
    SYS_SCHED_YIELD = 158, /* sched_yield() : cede la place aux autres programmes */
    SYS_LOG      = 199,  /* log(niveau, texte) : ecrit dans le journal du systeme (propre a PhoenOS ; 199 = appel sans effet sous Linux) */
    SYS_SLEEP_MS = 200,  /* sleep_ms(ms)    propre a PhoenOS */
    SYS_UPTIME   = 201,  /* uptime_ms()     propre a PhoenOS */
    SYS_SPAWN    = 202,  /* spawn(chemin, argv) : lance un programme enfant, renvoie son numero (propre a PhoenOS) */
    SYS_PSL      = 203   /* psl(nom, symbole) : charge la bibliotheque /lib/<nom>.psl si besoin, renvoie l'adresse du symbole (NULL : son adresse de base) (propre a PhoenOS) */
};

struct user_fd {
    struct fs_node *node;               /* NULL = libre */
    u32 pos;
    int writable;                       /* ouvert en ecriture (les ecritures vont en fin de fichier) */
    int readable;
};

struct uproc {
    int  running;
    int  exit_requested;                /* mis par SYS_EXIT / Ctrl+C : l'appelant doit rendre la main */
    int  exit_code;
    u32  pid;
    char name[32];
    u32  win_lo, win_hi;                /* seule zone que le programme peut passer au noyau */
    struct aspace *as;                  /* ses pages reellement mappees (NULL : seule la fenetre compte, tests) */
    u32  ppid;                          /* programme qui l'a lance (0 : le shell) */
    u32  heap_lo, heap_cur, heap_hi;    /* tas (brk) : debut, fin actuelle, limite ; heap_hi == 0 : pas de tas */
    u8   nlibs;                         /* bibliotheques .psl utilisees (indices du gestionnaire, libmgr.h) */
    u8   libs[PSL_PER_PROC];
    struct user_fd fd[USER_MAX_FDS];
    const u8 *in;                       /* entree standard issue d'un tube (sinon in_set == 0) */
    u32  in_len, in_pos;
    int  in_set;
    char line[256];                     /* ligne en cours de lecture au clavier */
    u32  line_len, line_pos;
};

/* Noyau : reserve la zone. E_BUSY / E_NOMEM : les programmes seront indisponibles. */
err_t user_init(void);
int   user_available(void);

/* Bibliotheques .psl que le prochain programme lance doit recevoir (celles de l'en-tete .poe) ; consommees par ce lancement. */
void user_set_needs(char needs[][POE_LIBNAME_MAX], int n);

/* Entree standard du prochain programme : sortie du tube precedent (set = 0 : clavier). */
void user_set_stdin(const u8 *buf, u32 len, int set);

struct user_result {
    int exit_code;                      /* code de exit(), ou 128 + numero d'exception si arrete */
    int killed;                         /* 1 : arrete par le noyau (faute) ou Ctrl+C */
};

/* Charge `image` et le lance en arriere-plan (detache) ; *pid = son numero. Memes erreurs que user_exec. */
err_t user_spawn_bg(const u8 *image, u32 size, const char *name, int argc, char **argv, u32 *pid);

/*
 * Programmes qui en lancent d'autres (0.7.0).
 * user_spawn_child : charge le fichier `path` (ELF ou .poe ; sans '/', cherche aussi dans /bin) et le lance comme
 *   enfant de `parent`, en arriere-plan et sans message. Son parent doit l'attendre (user_waitpid) ; si le parent
 *   s'arrete d'abord, l'enfant continue seul. Erreurs : celles de user_spawn_bg + E_NOENT / E_ISDIR (fichier).
 * user_waitpid : attend la fin d'un enfant de `parent` (pid -1 : n'importe lequel ; WNOHANG : 0 s'il tourne encore).
 *   Renvoie son numero et met son code de sortie dans *code, ou E_CHILD (pas un enfant) / E_INTR (parent arrete).
 */
err_t user_spawn_child(struct uproc *parent, const char *path, int argc, char **argv, u32 *pid);
i32   user_waitpid(struct uproc *parent, i32 pid, u32 options, int *code);

/*
 * Pour le shell (`wait`) : attend que le programme `pid` (lance en arriere-plan) soit termine. Renvoie E_NOENT si
 * on ne le connait pas, E_INTR si Ctrl+C. *code = code de sortie (si connu : les 32 derniers programmes sont retenus).
 */
err_t user_wait_pid(u32 pid, int *code, int *known);

/* Comme user_kill, mais arrete aussi tous les programmes lances par `pid` (Ctrl+C et la commande kill font ca). */
err_t user_kill_tree(u32 pid);

/* Demande l'arret du programme `pid` (E_NOENT : inconnu ; E_INVAL : pas un programme). Il s'arrete des qu'il reprend la main. */
err_t user_kill(u32 pid);

/* Pose les liens avec l'ordonnanceur (commutation, attente clavier). A appeler apres sched_init. */
void user_sched_init(void);

/* Appele par isr.c apres l'EOI de chaque tick de la minuterie : preemption d'un programme en ring 3. */
void user_preempt(void);

/*
 * Charge et execute `image` (fichier ELF) ; rend la main a la fin du programme.
 * E_NOSYS : indisponible ; E_NOEXEC : ELF invalide ; E_BUSY : table des taches pleine ; E_2BIG : arguments.
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
