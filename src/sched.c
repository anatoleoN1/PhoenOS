/* PhoenOS - sched.c : ordonnanceur a tourniquet (voir sched.h pour les regles). */
#include "sched.h"
#include "io.h"
#include "kheap.h"
#include "string.h"

static struct task *cur;
static u32 next_id = 1;
static u32 switches;
static int ready_flag;

/* Journal des derniers evenements (commande schedlog) : pour comprendre ce qui se passe sur une vraie machine. */
#define TRACE_N 128
static struct { u32 t; char k; u8 a, b; } trace_buf[TRACE_N];
static u32 trace_n;
static u32 idle_loops;

void (*sched_event_hook)(char kind, u32 id_a, u32 id_b, const char *name_a);

static struct task tasks[SCHED_MAX_TASKS];

static void trace(char k, u32 a, u32 b)
{
    u32 i = trace_n++ % TRACE_N;

    if (sched_event_hook)                       /* ids (et non plus indices) pour le journal de debogage */
        sched_event_hook(k, tasks[a % SCHED_MAX_TASKS].id, k == 'S' ? tasks[b % SCHED_MAX_TASKS].id : b,
                         tasks[a % SCHED_MAX_TASKS].name);

    trace_buf[i].t = pit_ticks();
    trace_buf[i].k = k;
    trace_buf[i].a = (u8)a;
    trace_buf[i].b = (u8)b;
}

int sched_trace_get(int index, u32 *tick, char *kind, u32 *a, u32 *b)
{
    u32 total = trace_n < TRACE_N ? trace_n : TRACE_N;
    u32 first = trace_n - total;

    if (index < 0 || (u32)index >= total)
        return 0;
    *tick = trace_buf[(first + (u32)index) % TRACE_N].t;
    *kind = trace_buf[(first + (u32)index) % TRACE_N].k;
    *a = trace_buf[(first + (u32)index) % TRACE_N].a;
    *b = trace_buf[(first + (u32)index) % TRACE_N].b;
    return 1;
}

u32 sched_idle_count(void)
{
    return idle_loops;
}

void (*sched_switch_hook)(struct task *next);
void (*sched_idle_hook)(void);

void sched_init(void)
{
    memset(tasks, 0, sizeof tasks);
    tasks[0].id = 0;
    strcpy(tasks[0].name, "shell");
    tasks[0].state = T_RUNNING;
    cur = &tasks[0];
    next_id = 1;
    switches = 0;
    ready_flag = 1;
}

int sched_ready(void)
{
    return ready_flag;
}

struct task *sched_current(void)
{
    return cur;
}

struct task *sched_find(u32 id)
{
    for (int i = 0; i < SCHED_MAX_TASKS; i++)
        if (tasks[i].state != T_FREE && tasks[i].id == id)
            return &tasks[i];
    return NULL;
}

static int index_of(const struct task *t)
{
    return (int)(t - tasks);
}

/* Libere une tache terminee. Jamais la tache courante (on tourne sur sa pile). */
static void free_task(struct task *t)
{
    void (*cleanup)(void *) = t->cleanup;
    void *owner = t->owner;

    kfree(t->kstack);
    memset(t, 0, sizeof *t);                    /* T_FREE */
    if (cleanup)
        cleanup(owner);
}

/* A appeler au retour de toute commutation : libere les taches detachees deja terminees. */
static void reap_detached(void)
{
    for (int i = 1; i < SCHED_MAX_TASKS; i++)
        if (tasks[i].state == T_ZOMBIE && tasks[i].detached && &tasks[i] != cur)
            free_task(&tasks[i]);
}

/* Prochaine tache a faire tourner (autre que la courante) ; la reveille si besoin. NULL si aucune. */
static struct task *pick_next(void)
{
    int ci = index_of(cur);

    for (int k = 1; k < SCHED_MAX_TASKS; k++) {
        struct task *t = &tasks[(ci + k) % SCHED_MAX_TASKS];

        if (t->state == T_READY)
            return t;
        if (t->state == T_BLOCKED && (t->killed || t->pred(t->pred_arg)))
            return t;
    }
    return NULL;
}

/* Interruptions coupees par l'appelant. */
static void switch_to(struct task *next)
{
    struct task *prev = cur;

    if (prev->state == T_RUNNING)
        prev->state = T_READY;
    next->state = T_RUNNING;
    cur = next;
    switches++;
    trace('S', (u32)index_of(prev), (u32)index_of(next));
    if (sched_switch_hook)
        sched_switch_hook(next);
    switch_context(&prev->kesp, next->kesp);
    reap_detached();                            /* on est de retour sur NOTRE pile */
}

void sched_yield(void)
{
    u32 f = cpu_irq_save();
    struct task *n = pick_next();

    if (n)
        switch_to(n);
    cpu_irq_restore(f);
}

void sched_preempt(void)
{
    u32 f = cpu_irq_save();
    struct task *n;

    cur->ticks++;
    n = pick_next();
    if (n)
        switch_to(n);
    cpu_irq_restore(f);
}

void sched_block(int (*pred)(void *), void *arg)
{
    u32 f = cpu_irq_save();
    int traced = 0;

    for (;;) {
        struct task *n;

        if (cur->killed || pred(arg))
            break;
        cur->pred = pred;
        cur->pred_arg = arg;
        if (!traced) {
            trace('B', (u32)index_of(cur), 0);
            traced = 1;
        }
        cur->state = T_BLOCKED;
        n = pick_next();
        if (n) {
            switch_to(n);                       /* on revient ici quand quelqu'un nous a choisis */
            continue;
        }
        idle_loops++;
        if (sched_idle_hook)
            sched_idle_hook();
        else
            cpu_sti_hlt();                      /* rien a faire : le processeur dort jusqu'a la prochaine interruption */
        cpu_cli();
    }
    if (traced)
        trace('W', (u32)index_of(cur), 0);
    cur->state = T_RUNNING;
    cur->pred = NULL;
    cpu_irq_restore(f);
}

struct sleep_arg { u32 target; };

static int sleep_done(void *p)
{
    return (i32)(pit_ticks() - ((struct sleep_arg *)p)->target) >= 0;
}

void sched_sleep_ms(u32 ms)
{
    struct sleep_arg a;

    a.target = pit_ticks() + (ms * pit_hz()) / 1000;
    sched_block(sleep_done, &a);
}

void sched_exit(int code)
{
    cpu_cli();
    cur->exit_code = code;
    cur->state = T_ZOMBIE;
    trace('X', (u32)index_of(cur), (u32)code);
    for (;;) {
        struct task *n = pick_next();

        if (n)
            switch_to(n);                       /* un zombie n'est jamais choisi : on ne revient pas */
        else if (sched_idle_hook)
            sched_idle_hook();
        else
            cpu_sti_hlt();
        cpu_cli();
    }
}

static int is_zombie(void *p)
{
    return ((struct task *)p)->state == T_ZOMBIE;
}

int sched_wait(struct task *t)
{
    sched_block(is_zombie, t);
    return t->exit_code;
}

void sched_reap(struct task *t)
{
    if (t != cur && t->state == T_ZOMBIE && index_of(t) > 0)
        free_task(t);
}

void sched_detach(struct task *t)
{
    t->detached = 1;
}

/* Enfants d'un programme : les taches non detachees dont il est le parent. */
int sched_children(u32 parent, u32 *zombie)
{
    int n = 0;

    if (zombie)
        *zombie = 0;
    for (int i = 1; i < SCHED_MAX_TASKS; i++) {
        struct task *t = &tasks[i];

        if (t->state == T_FREE || t->detached || t->parent != parent || parent == 0)
            continue;
        n++;
        if (t->state == T_ZOMBIE && zombie && !*zombie)
            *zombie = t->id;
    }
    return n;
}

int sched_kill_tree(struct task *t)
{
    int n = sched_kill(t) == E_OK;

    if (!t || t->id == 0)
        return 0;
    for (int i = 1; i < SCHED_MAX_TASKS; i++)
        if (&tasks[i] != t && tasks[i].state != T_FREE && tasks[i].parent == t->id)
            n += sched_kill_tree(&tasks[i]);
    return n;
}

void sched_orphan(u32 parent)
{
    if (parent == 0)
        return;
    for (int i = 1; i < SCHED_MAX_TASKS; i++)
        if (tasks[i].state != T_FREE && tasks[i].parent == parent) {
            tasks[i].parent = 0;
            tasks[i].detached = 1;              /* un zombie sera libere au prochain changement de tache */
        }
}

int sched_kill(struct task *t)
{
    if (!t || index_of(t) == 0 || t->state == T_ZOMBIE || t->state == T_FREE)
        return E_INVAL;
    t->killed = 1;
    return E_OK;
}

static void task_start(void)
{
    struct task *t = cur;

    reap_detached();
    cpu_sti();
    if (!t->killed)
        t->entry(t->arg);
    sched_exit(t->killed ? 130 : 0);
}

err_t sched_create(const char *name, void (*entry)(void *), void *arg, void (*cleanup)(void *owner),
                   void *owner, struct task **out)
{
    struct task *t = NULL;
    u32 *sp;
    u32 top;

    for (int i = 1; i < SCHED_MAX_TASKS; i++)
        if (tasks[i].state == T_FREE) {
            t = &tasks[i];
            break;
        }
    if (!t)
        return E_BUSY;
    memset(t, 0, sizeof *t);
    t->kstack = kmalloc(SCHED_KSTACK_SIZE);
    if (!t->kstack)
        return E_NOMEM;

    t->id = next_id++;
    for (usize i = 0; i + 1 < sizeof t->name && name[i]; i++)
        t->name[i] = name[i];
    t->entry = entry;
    t->arg = arg;
    t->cleanup = cleanup;
    t->owner = owner;

    top = ((u32)t->kstack + SCHED_KSTACK_SIZE) & ~15u;
    t->ktop = top;
    sp = (u32 *)top;
    *--sp = 0;                                  /* adresse de retour factice de task_start (ne revient jamais) */
    *--sp = (u32)task_start;                    /* le "ret" de switch_context mene ici */
    *--sp = 0x2;                                /* EFLAGS : interruptions coupees jusqu'a task_start */
    *--sp = 0;                                  /* ebp */
    *--sp = 0;                                  /* ebx */
    *--sp = 0;                                  /* esi */
    *--sp = 0;                                  /* edi */
    t->kesp = (u32)sp;
    t->state = T_READY;
    if (sched_event_hook)
        sched_event_hook('C', t->id, 0, t->name);
    if (out)
        *out = t;
    return E_OK;
}

int sched_info(int index, struct task_info *out)
{
    int n = 0;

    for (int i = 0; i < SCHED_MAX_TASKS; i++) {
        if (tasks[i].state == T_FREE)
            continue;
        if (n++ == index) {
            out->id = tasks[i].id;
            memcpy(out->name, tasks[i].name, SCHED_NAME_MAX);
            out->state = tasks[i].state;
            out->ticks = tasks[i].ticks;
            out->detached = tasks[i].detached;
            out->parent = tasks[i].parent;
            return 1;
        }
    }
    return 0;
}

u32 sched_switches(void)
{
    return switches;
}
