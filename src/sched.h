#ifndef PHOENOS_SCHED_H
#define PHOENOS_SCHED_H

#include "types.h"
#include "error.h"

/*
 * Ordonnanceur (0.6.0) : plusieurs taches se partagent le processeur.
 *
 * Chaque tache a SA pile noyau (16 Kio) ; la commutation echange simplement de pile
 * (switch.S). La tache 0 est le contexte de demarrage : le shell.
 *
 * Regles (simples a raisonner, donc sures) :
 *  - le noyau n'est JAMAIS interrompu au milieu d'un appel systeme ou d'une commande : une tache en
 *    mode noyau ne cede la place que de son plein gre (sched_yield / sched_block / sched_exit) ;
 *  - un programme en ring 3 est preempte par la minuterie (sched_preempt) : tourniquet, 10 ms par tour ;
 *  - une tache qui attend (touche, delai, fin d'un programme...) est BLOQUEE par un predicat : le
 *    planificateur ne la reveille que lorsque le predicat est vrai. Rien d'actif a attendre = le processeur dort (hlt).
 */
#define SCHED_MAX_TASKS   8             /* shell compris */
#define SCHED_KSTACK_SIZE 16384
#define SCHED_NAME_MAX    32

enum task_state { T_FREE = 0, T_READY, T_RUNNING, T_BLOCKED, T_ZOMBIE };

struct task {
    u32  id;                            /* numero de processus (pid) ; 0 = shell */
    char name[SCHED_NAME_MAX];
    int  state;
    u32  kesp;                          /* ESP noyau sauvegarde quand la tache ne tourne pas */
    u8  *kstack;                        /* bloc de pile (NULL pour la tache 0 : pile de demarrage) */
    u32  ktop;                          /* sommet de la pile noyau (valeur de tss.esp0) */
    int (*pred)(void *);                /* tache bloquee : se reveille quand pred(pred_arg) != 0 */
    void *pred_arg;
    void (*entry)(void *);
    void *arg;
    void (*cleanup)(void *owner);       /* appele quand la tache est liberee */
    void *owner;                        /* donnees du proprietaire (user.c) */
    int  exit_code;
    int  detached;                      /* personne n'attend sa fin : liberee automatiquement */
    int  killed;                        /* arret demande (kill) : prise en compte au prochain point sur */
    u32  ticks;                         /* tours de minuterie recus en tournant */
};

struct task_info {
    u32  id;
    char name[SCHED_NAME_MAX];
    int  state;
    u32  ticks;
    int  detached;
};

/* Fait du contexte actuel la tache 0 ("shell"). A appeler une fois. */
void sched_init(void);
int  sched_ready(void);

/*
 * Cree une tache qui executera entry(arg) puis se terminera. Elle est prete mais ne tourne qu'au
 * prochain tour. E_BUSY : table pleine ; E_NOMEM : pas de memoire pour sa pile.
 */
err_t sched_create(const char *name, void (*entry)(void *), void *arg, void (*cleanup)(void *owner),
                   void *owner, struct task **out);

struct task *sched_current(void);
struct task *sched_find(u32 id);

void sched_yield(void);                                     /* cede la place s'il y a une autre tache prete */
void sched_block(int (*pred)(void *), void *arg);           /* attend que pred(arg) soit vrai (ou kill) */
void sched_sleep_ms(u32 ms);                                /* attend ms millisecondes sans bloquer les autres */
void sched_exit(int code) __attribute__((noreturn));        /* termine la tache courante */
int  sched_wait(struct task *t);                            /* attend la fin de t ; renvoie son code de sortie */
void sched_reap(struct task *t);                            /* libere une tache terminee (apres sched_wait) */
void sched_detach(struct task *t);                          /* personne n'attendra t : liberation automatique */
int  sched_kill(struct task *t);                            /* demande l'arret ; E_INVAL pour la tache 0 / terminee */
void sched_preempt(void);                                   /* appele par la minuterie quand un programme tourne en ring 3 */
int  sched_info(int index, struct task_info *out);          /* index 0.. : toutes les taches non libres ; 0 = fin */
u32  sched_switches(void);                                  /* nombre de commutations (statistiques / tests) */

/* Reglages (materiel) */
extern void (*sched_switch_hook)(struct task *next);        /* avant chaque commutation : tss.esp0, CR3 */
extern void (*sched_idle_hook)(void);                       /* a la place de sti+hlt quand rien n'est pret (tests) */

/* Fournis ailleurs */
void switch_context(u32 *save_esp, u32 new_esp);
u32  pit_hz(void);
u32  pit_ticks(void);

#endif
