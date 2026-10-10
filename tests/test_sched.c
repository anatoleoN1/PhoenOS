/* Test du vrai sched.c avec de VRAIES commutations de pile (switch.S) : taches du noyau sur la machine de test. */
#include "testlib.h"
#include "sched.h"
#include "kheap.h"
#include "pmm.h"
#include "multiboot.h"

u8 test_scancode;
char kernel_start[4096] __attribute__((aligned(4096)));
char kernel_end[1];
void test_panic(const char *fmt, ...) { out("  PANIQUE INATTENDUE : "); out(fmt); out("\n"); exit_now(2); }
static char fake_ram[4u << 20] __attribute__((aligned(4096)));

static u32 ticks;
u32 pit_ticks(void) { return ticks; }
u32 pit_hz(void) { return 100; }
static u32 idle_calls;
int key_wake_after;
static int flag;
static void idle(void) { idle_calls++; ticks++; if (key_wake_after && idle_calls % key_wake_after == 0) flag = 1; }                      /* "le temps passe" quand tout dort */

static char logb[512];
static int logn;
static void lg(char c) { if (logn < (int)sizeof logb - 1) logb[logn++] = c; logb[logn] = 0; }
static int log_is(const char *s) { return strcmp(logb, s) == 0; }
static void log_reset(void) { logn = 0; logb[0] = 0; }

static void yielder(void *arg)
{
    char c = *(char *)arg;

    for (int i = 0; i < 3; i++) { lg(c); sched_yield(); }
}

static int flag_set(void *p) { (void)p; return flag; }
static void blocker(void *arg)
{
    (void)arg;
    lg('['); sched_block(flag_set, NULL); lg(']');
}

static void sleeper(void *arg)
{
    sched_sleep_ms(*(u32 *)arg);
    lg('z');
}

static void coder(void *arg)
{
    int code = *(int *)arg;

    sched_exit(code);
}

static int cleanups;
static void cleanup_cb(void *owner) { cleanups += (int)(long)owner; }

static int never(void *p) { (void)p; return 0; }
static void victim(void *arg)
{
    (void)arg;
    sched_block(never, NULL);                                           /* ne se reveille que par kill */
    lg(sched_current()->killed ? 'K' : '?');
}

/* chaque tache remplit une grande zone locale d'un motif a elle, cede la place, puis verifie */
static int stack_bad;
static void stacker(void *arg)
{
    u8 buf[6000];
    u8 pat = *(u8 *)arg;

    memset(buf, pat, sizeof buf);
    for (int i = 0; i < 10; i++) {
        sched_yield();
        for (int k = 0; k < (int)sizeof buf; k++)
            if (buf[k] != pat) { stack_bad++; break; }
    }
}

static void spin_preempt(void *arg)
{
    (void)arg;
    for (int i = 0; i < 5; i++) { lg('p'); sched_preempt(); }
}

static void nop(void *a) { (void)a; }

int test_main(void)
{
    static struct { struct multiboot_info mbi; struct multiboot_mmap_entry e[1]; } M;
    struct task *a, *b, *c, *t;
    struct task_info ti;
    char ca = 'a', cb = 'b';

    M.mbi.flags = MB_FLAG_MMAP;
    M.mbi.mmap_addr = (u32)M.e;
    M.mbi.mmap_length = sizeof M.e;
    M.e[0] = (struct multiboot_mmap_entry){ 20, (u32)fake_ram, 0, sizeof fake_ram, 0, MB_MMAP_AVAILABLE };
    pmm_init(&M.mbi);
    kheap_init();
    sched_idle_hook = idle;

    /* ---- la tache 0 est le contexte actuel ---- */
    CHECK(!sched_ready());
    sched_init();
    CHECK(sched_ready() && sched_current() && sched_current()->id == 0 && !strcmp(sched_current()->name, "shell"));
    CHECK(sched_current()->state == T_RUNNING && sched_find(0) == sched_current() && sched_find(7) == NULL);
    sched_yield();                                                      /* personne d'autre : sans effet */
    CHECK(sched_switches() == 0);

    /* ---- tourniquet : a, b, a, b... dans l'ordre de creation ---- */
    CHECK(sched_create("A", yielder, &ca, NULL, NULL, &a) == E_OK && a->id == 1 && a->state == T_READY);
    CHECK(sched_create("B", yielder, &cb, NULL, NULL, &b) == E_OK && b->id == 2);
    CHECK(a->kstack && a->ktop % 16 == 0 && a->ktop > (u32)a->kstack && a->ktop <= (u32)a->kstack + SCHED_KSTACK_SIZE);
    log_reset();
    sched_wait(a);
    sched_wait(b);
    CHECK(log_is("ababab"));
    CHECK(a->state == T_ZOMBIE && b->state == T_ZOMBIE && a->exit_code == 0);
    sched_reap(a); sched_reap(b);
    CHECK(a->state == T_FREE && b->state == T_FREE && sched_find(1) == NULL);
    CHECK(sched_switches() > 0);

    /* ---- attente par predicat : ne tourne pas tant que le predicat est faux ---- */
    flag = 0;
    log_reset();
    CHECK(sched_create("C", blocker, NULL, NULL, NULL, &c) == E_OK && c->id == 3);       /* les pid ne sont jamais reutilises */
    sched_yield();                                                      /* C demarre et se bloque */
    CHECK(log_is("[") && c->state == T_BLOCKED);
    sched_yield(); sched_yield();
    CHECK(log_is("[") && c->state == T_BLOCKED);                        /* toujours bloque */
    flag = 1;
    sched_yield();                                                      /* le predicat est vrai : C repart */
    CHECK(log_is("[]") && c->state == T_ZOMBIE);
    sched_reap(c);

    /* ---- dormir : le temps avance quand tout le monde dort (hook), et on se reveille apres le delai ---- */
    {
        u32 d1 = 30, d2 = 80, t0 = ticks;
        struct task *s1, *s2;

        log_reset(); idle_calls = 0;
        CHECK(sched_create("S1", sleeper, &d1, NULL, NULL, &s1) == E_OK);
        CHECK(sched_create("S2", sleeper, &d2, NULL, NULL, &s2) == E_OK);
        sched_wait(s1);
        CHECK(log_is("z") && ticks - t0 >= 3 && ticks - t0 < 8);        /* le plus court d'abord (30 ms = 3 ticks) */
        sched_wait(s2);
        CHECK(log_is("zz") && ticks - t0 >= 8 && idle_calls > 0);
        sched_reap(s1); sched_reap(s2);
        t0 = ticks;
        sched_sleep_ms(50);                                             /* la tache 0 aussi peut dormir */
        CHECK(ticks - t0 >= 5);
        sched_sleep_ms(0);
    }

    /* ---- la tache 0 attend une "touche" pendant qu'une autre dort : le reveil doit venir du ralenti ---- */
    {
        u32 d = 500, t0 = ticks;
        struct task *s;
        extern int key_wake_after;

        flag = 0;
        key_wake_after = 4;                                             /* le 4e passage au ralenti = "touche tapee" */
        CHECK(sched_create("S", sleeper, &d, NULL, NULL, &s) == E_OK);
        log_reset();
        sched_block(flag_set, NULL);
        CHECK(flag == 1 && ticks - t0 < 10 && s->state != T_ZOMBIE);   /* reveillee bien avant la fin du sommeil (50 ticks) */
        key_wake_after = 0;
        sched_wait(s);
        CHECK(log_is("z") && ticks - t0 >= 50);
        sched_reap(s);
    }

    /* ---- codes de sortie, liberation, reutilisation de la place ---- */
    {
        int c7 = 7, c300 = 300;
        struct task *x, *y;

        CHECK(sched_create("X", coder, &c7, NULL, NULL, &x) == E_OK && sched_create("Y", coder, &c300, NULL, NULL, &y) == E_OK);
        CHECK(sched_wait(x) == 7 && sched_wait(y) == 300);
        sched_reap(x); sched_reap(y);
        sched_reap(x);                                                  /* deux fois : sans danger */
        sched_reap(sched_current());                                    /* jamais la tache courante / le shell */
        CHECK(sched_current()->state == T_RUNNING);
    }

    /* ---- taches detachees : liberees toutes seules, cleanup appele ---- */
    {
        int z = 0;
        struct task *d, *e;
        u32 did, eid;

        cleanups = 0;
        CHECK(sched_create("D", coder, &z, cleanup_cb, (void *)10, &d) == E_OK);
        CHECK(sched_create("E", coder, &z, cleanup_cb, (void *)100, &e) == E_OK);
        did = d->id; eid = e->id;
        sched_detach(d); sched_detach(e);
        sched_yield(); sched_yield(); sched_yield();
        CHECK(cleanups == 110 && sched_find(did) == NULL && sched_find(eid) == NULL);
        /* avec attente : cleanup appele par sched_reap, une seule fois */
        CHECK(sched_create("W", coder, &z, cleanup_cb, (void *)1000, &d) == E_OK);
        sched_wait(d);
        CHECK(cleanups == 110);                                         /* pas encore */
        sched_reap(d);
        CHECK(cleanups == 1110);
        sched_reap(d);
        CHECK(cleanups == 1110);
    }

    /* ---- kill : reveille une tache bloquee, qui voit killed ---- */
    {
        struct task *v;

        log_reset();
        CHECK(sched_create("V", victim, NULL, NULL, NULL, &v) == E_OK);
        sched_yield();
        CHECK(v->state == T_BLOCKED && log_is(""));
        CHECK(sched_kill(sched_find(0)) == E_INVAL && sched_kill(NULL) == E_INVAL);
        CHECK(sched_kill(v) == E_OK);
        sched_wait(v);
        CHECK(log_is("K") && v->exit_code == 130);
        CHECK(sched_kill(v) == E_INVAL);                                /* deja termine */
        sched_reap(v);
        /* tue avant meme d'avoir demarre : n'execute jamais son code, code de sortie 130 */
        CHECK(sched_create("V2", victim, NULL, NULL, NULL, &v) == E_OK);
        CHECK(sched_kill(v) == E_OK);
        CHECK(sched_wait(v) == 130 && log_is("K"));
        sched_reap(v);
    }

    /* ---- table pleine ---- */
    {
        struct task *ts[SCHED_MAX_TASKS];
        int n = 0;
        u32 free0 = kheap_size();

        (void)free0;
        while (n < SCHED_MAX_TASKS - 1 && sched_create("T", nop, NULL, NULL, NULL, &ts[n]) == E_OK)
            n++;
        CHECK(n == SCHED_MAX_TASKS - 1);
        CHECK(sched_create("trop", nop, NULL, NULL, NULL, &t) == E_BUSY);
        for (int i = 0; i < n; i++) { sched_wait(ts[i]); sched_reap(ts[i]); }
        CHECK(sched_create("encore", nop, NULL, NULL, NULL, &t) == E_OK);       /* les places sont revenues */
        sched_wait(t); sched_reap(t);
    }

    /* ---- chaque tache a sa pile : 4 taches, 6 Kio de motif chacune, 10 commutations ---- */
    {
        u8 pats[4] = { 0x11, 0x22, 0x33, 0x44 };
        struct task *ts[4];

        for (int i = 0; i < 4; i++)
            CHECK(sched_create("stk", stacker, &pats[i], NULL, NULL, &ts[i]) == E_OK);
        for (int i = 0; i < 4; i++) { sched_wait(ts[i]); sched_reap(ts[i]); }
        CHECK(stack_bad == 0);
    }

    /* ---- preemption (appelee par la minuterie) : cede la place et compte les tours ---- */
    {
        struct task *p1, *p2;
        u32 s0 = sched_switches();

        log_reset();
        CHECK(sched_create("P1", spin_preempt, NULL, NULL, NULL, &p1) == E_OK && sched_create("P2", spin_preempt, NULL, NULL, NULL, &p2) == E_OK);
        sched_wait(p1); sched_wait(p2);
        CHECK(strlen(logb) == 10 && p1->ticks == 5 && p2->ticks == 5);
        CHECK(sched_switches() - s0 >= 10);
        sched_reap(p1); sched_reap(p2);
    }

    /* ---- liste des taches ---- */
    {
        struct task *l1;
        int n = 0;

        CHECK(sched_create("liste", nop, NULL, NULL, NULL, &l1) == E_OK);
        CHECK(sched_info(0, &ti) == 1 && ti.id == 0 && !strcmp(ti.name, "shell") && ti.state == T_RUNNING);
        while (sched_info(n, &ti)) n++;
        CHECK(n == 2 && sched_info(1, &ti) == 1 && !strcmp(ti.name, "liste") && ti.state == T_READY && sched_info(2, &ti) == 0);
        sched_wait(l1); sched_reap(l1);
    }

    /* ---- endurance : 3 taches, 3000 commutations, aucune fuite de memoire ---- */
    {
        struct task *ts[3];
        static char cs[3] = { 'x', 'y', 'z' };
        u32 left0 = pmm_free_frames();

        for (int round = 0; round < 100; round++) {
            for (int i = 0; i < 3; i++)
                CHECK(sched_create("e", yielder, &cs[i], NULL, NULL, &ts[i]) == E_OK);
            for (int i = 0; i < 3; i++) { sched_wait(ts[i]); sched_reap(ts[i]); }
        }
        CHECK(pmm_free_frames() == left0);
    }

    out("  ordonnanceur verifie : vraies commutations de pile, tourniquet, predicats, sommeil, sortie, kill, detachement, table pleine, piles isolees\n");
    return 0;
}
