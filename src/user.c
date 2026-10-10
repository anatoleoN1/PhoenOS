/*
 * PhoenOS - user.c
 * Lancement des programmes utilisateur : espace d'adresses, pile et arguments, tache de
 * l'ordonnanceur, passage au ring 3, fin du programme (exit, Ctrl+C, kill, faute du processeur).
 *
 * Le programme est entierement charge par le PARENT (aspace_put : on ecrit dans les cadres sans
 * changer de CR3) ; la tache n'a plus qu'a sauter en ring 3. A chaque commutation, le crochet
 * regle la pile du noyau du CPU (tss.esp0) et le repertoire de pages (CR3) de la tache choisie.
 */
#include "user.h"
#include "elf.h"
#include "aspace.h"
#include "paging.h"
#include "pmm.h"
#include "kheap.h"
#include "kprintf.h"
#include "keyboard.h"
#include "sched.h"
#include "vga.h"
#include "string.h"

extern struct { u32 prev, esp0; } tss;          /* debut de la structure de gdt.c */
extern void enter_user_mode(u32 entry, u32 user_esp);   /* switch.S : ne revient jamais */

#define CTRL_C 3

struct utask {
    struct uproc proc;
    struct aspace space;
    struct task  *task;
    u32 entry, esp;
    int bg;
    int faulted;                                /* arrete par une faute du processeur */
};

static int user_ok;
static const u8 *pending_in;
static u32 pending_in_len;
static int pending_in_set;

err_t user_init(void)
{
    err_t e = pmm_reserve_exact(USER_BASE, USER_END);

    user_ok = (e == E_OK);
    return e;
}

int user_available(void)
{
    return user_ok;
}

void user_set_stdin(const u8 *buf, u32 len, int set)
{
    pending_in = buf;
    pending_in_len = len;
    pending_in_set = set;
}

static void user_task_main(void *arg);

static struct utask *current_utask(void)
{
    struct task *t;

    if (!sched_ready())
        return NULL;
    t = sched_current();
    return (t && t->owner && t->entry == user_task_main) ? (struct utask *)t->owner : NULL;
}

struct uproc *user_current(void)
{
    struct utask *u = current_utask();

    return u ? &u->proc : NULL;
}

/* ---- pile initiale ---- */

#define STACK_TMP 8192u                         /* 16 mots + 4 Kio de textes + pointeurs, largement */
#define STACK_TMP_BASE (USER_END - STACK_TMP)

/* Construit argc, argv[], NULL puis les chaines dans `mem` (qui represente [STACK_TMP_BASE, USER_END)). */
static err_t build_stack(u8 *mem, int argc, char **argv, u32 *esp)
{
    u32 total = 0, sp, argv_addr;
    u32 ptrs[USER_ARGS_MAX];

    if (argc < 0 || argc > USER_ARGS_MAX)
        return E_2BIG;
    for (int i = 0; i < argc; i++) {
        total += (u32)strlen(argv[i]) + 1;
        if (total > USER_ARGS_BYTES)
            return E_2BIG;
    }
    sp = USER_END;
    for (int i = argc - 1; i >= 0; i--) {
        u32 len = (u32)strlen(argv[i]) + 1;

        sp -= len;
        memcpy(mem + (sp - STACK_TMP_BASE), argv[i], len);
        ptrs[i] = sp;
    }
    sp &= ~15u;
    sp -= 4 * ((u32)argc + 1);                  /* argv[0..argc-1] et NULL */
    argv_addr = sp;
    for (int i = 0; i < argc; i++)
        memcpy(mem + (argv_addr - STACK_TMP_BASE) + 4u * (u32)i, &ptrs[i], 4);
    memset(mem + (argv_addr - STACK_TMP_BASE) + 4u * (u32)argc, 0, 4);
    sp -= 4;
    memcpy(mem + (sp - STACK_TMP_BASE), &argc, 4);
    *esp = sp;
    return E_OK;
}

_Static_assert(USER_BASE == ASPACE_BASE && USER_END == ASPACE_END, "zone des programmes = une table de pages");

static err_t load_put(void *ctx, u32 va, const u8 *src, u32 n)
{
    return aspace_put((struct aspace *)ctx, va, src, n);
}

/* ---- crochets de l'ordonnanceur ---- */

static void on_switch(struct task *next)
{
    tss.esp0 = next->ktop;                      /* pile du noyau quand ce programme sera interrompu */
    if (next->owner && next->entry == user_task_main)
        aspace_activate(&((struct utask *)next->owner)->space);
    else
        aspace_activate_kernel();
}

static int key_ready(void *p)
{
    (void)p;
    return keyboard_peek() >= 0;
}

static int wait_key(void)
{
    sched_block(key_ready, NULL);
    return sched_current()->killed;
}

void user_sched_init(void)
{
    sched_switch_hook = on_switch;
    keyboard_wait_hook = wait_key;
}

/* ---- vie et mort d'un programme ---- */

/* Libere les pages et la structure (appele quand la tache est liberee, jamais depuis elle-meme). */
static void user_cleanup(void *owner)
{
    struct utask *u = owner;

    aspace_destroy(&u->space);
    kfree(u);
}

/* Fin du programme courant, depuis son propre contexte noyau. Ne revient pas. */
static void user_exit(int code)
{
    struct utask *u = current_utask();

    if (u && u->bg) {
        vga_set_color(VGA_YELLOW, VGA_BLACK);
        if (code == 0)
            kprintf("\n[%u] %s termine\n", u->proc.pid, u->proc.name);
        else if (code == 130 || u->faulted)
            kprintf("\n[%u] %s arrete\n", u->proc.pid, u->proc.name);
        else
            kprintf("\n[%u] %s termine (code %d)\n", u->proc.pid, u->proc.name, code);
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    }
    sched_exit(code);
}

static void user_task_main(void *arg)
{
    struct utask *u = arg;

    enter_user_mode(u->entry, u->esp);
}

void user_preempt(void)
{
    if (!current_utask())
        return;
    if (sched_current()->killed)
        user_exit(130);
    sched_preempt();
}

err_t user_kill(u32 pid)
{
    struct task *t = sched_find(pid);

    if (!t)
        return E_NOENT;
    if (!t->owner || t->entry != user_task_main)
        return E_INVAL;
    return sched_kill(t);
}

static err_t user_spawn(const u8 *image, u32 size, const char *name, int argc, char **argv, int bg,
                        struct utask **out)
{
    static u8 stack_tmp[STACK_TMP];
    struct elf_seg segs[ELF_MAX_SEGS];
    struct utask *u;
    int nseg = 0;
    u32 entry, esp;
    u32 stack_lo = USER_END - USER_STACK_SIZE;
    err_t e;

    if (!user_ok || !paging_enabled() || !sched_ready())
        return E_NOSYS;
    e = elf_segments(image, size, USER_BASE, stack_lo, segs, &nseg, &entry);   /* valide avant de rien reserver */
    if (e != E_OK)
        return e;
    u = kmalloc(sizeof *u);
    if (!u)
        return E_NOMEM;
    memset(u, 0, sizeof *u);
    e = aspace_create(&u->space);
    if (e != E_OK) {
        kfree(u);
        return e;
    }
    for (int i = 0; i < nseg && e == E_OK; i++)
        e = aspace_map(&u->space, segs[i].lo, segs[i].hi, 1);       /* en ecriture le temps du chargement */
    if (e == E_OK)
        e = aspace_map(&u->space, stack_lo, USER_END, 1);
    if (e == E_OK)
        e = elf_load_with(image, size, load_put, &u->space, USER_BASE, stack_lo, &entry);
    if (e == E_OK)
        e = build_stack(stack_tmp, argc, argv, &esp);
    if (e == E_OK)
        e = aspace_put(&u->space, esp, stack_tmp + (esp - STACK_TMP_BASE), USER_END - esp);
    if (e != E_OK) {
        aspace_destroy(&u->space);
        kfree(u);
        return e;
    }
    for (int i = 0; i < nseg; i++)              /* code et constantes : lecture seule (W^X partiel) */
        if (!segs[i].writable)
            aspace_protect(&u->space, segs[i].lo, segs[i].hi, 0);

    user_proc_reset(&u->proc, name);
    u->proc.as = &u->space;
    if (bg) {
        u->proc.in = NULL;                      /* pas de clavier en arriere-plan : fin de fichier */
        u->proc.in_len = 0;
        u->proc.in_set = 1;
    } else {
        u->proc.in = pending_in;
        u->proc.in_len = pending_in_len;
        u->proc.in_set = pending_in_set;
    }
    pending_in_set = 0;
    u->proc.running = 1;
    u->entry = entry;
    u->esp = esp;
    u->bg = bg;

    e = sched_create(name, user_task_main, u, user_cleanup, u, &u->task);
    if (e != E_OK) {
        aspace_destroy(&u->space);
        kfree(u);
        return e;
    }
    u->proc.pid = u->task->id;
    *out = u;
    return E_OK;
}

err_t user_spawn_bg(const u8 *image, u32 size, const char *name, int argc, char **argv, u32 *pid)
{
    struct utask *u;
    err_t e = user_spawn(image, size, name, argc, argv, 1, &u);

    if (e != E_OK)
        return e;
    *pid = u->task->id;
    sched_detach(u->task);                      /* liberee toute seule a sa fin */
    return E_OK;
}

static int fg_done(void *p)
{
    return ((struct task *)p)->state == T_ZOMBIE || keyboard_peek() == CTRL_C;
}

err_t user_exec(const u8 *image, u32 size, const char *name, int argc, char **argv,
                struct user_result *res)
{
    struct utask *u;
    struct task *t;
    err_t e = user_spawn(image, size, name, argc, argv, 0, &u);

    if (e != E_OK)
        return e;
    t = u->task;
    for (;;) {
        sched_block(fg_done, t);
        if (t->state == T_ZOMBIE)
            break;
        keyboard_try_getc();                    /* Ctrl+C en attente : on le consomme et on arrete le programme */
        kprintf("^C\n");
        sched_kill(t);
    }
    res->exit_code = t->exit_code;
    res->killed = u->faulted || t->exit_code == 130;
    sched_reap(t);                              /* libere pages et structure */
    return E_OK;
}

void syscall_entry(struct regs *r)
{
    struct uproc *p = user_current();

    if (!p || (r->cs & 3) != 3) {               /* int 0x80 hors programme : refuse */
        r->eax = (u32)E_NOSYS;
        return;
    }
    __asm__ volatile ("sti");                   /* le clavier et le timer doivent continuer */
    r->eax = (u32)syscall_dispatch(p, r->eax, r->ebx, r->ecx, r->edx);
    if (p->exit_requested)
        user_exit(p->exit_code);
    if (sched_current()->killed)
        user_exit(130);
}

void user_fault(struct regs *r, const char *what)
{
    struct utask *u = current_utask();

    if (!u)
        return;                                 /* l'appelant retombera sur la panique */
    vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
    keprintf("\n%s : programme arrete - %s (exception %u, EIP=%#x)\n", u->proc.name, what, r->int_no, r->eip);
    if (r->int_no == 14) {
        u32 cr2;

        __asm__ volatile ("movl %%cr2, %0" : "=r"(cr2));
        keprintf("  adresse fautive : %#x (%s)\n", cr2,
                 (r->err_code & 1) ? "acces interdit : memoire du noyau ou page protegee"
                                   : "page absente");
    }
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    u->faulted = 1;
    user_exit(128 + (int)r->int_no);
}
