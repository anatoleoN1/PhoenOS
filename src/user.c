/*
 * PhoenOS - user.c
 * Lancement des programmes utilisateur : zone memoire, pile et arguments, passage au ring 3,
 * fin du programme (exit, Ctrl+C, faute du processeur).
 */
#include "user.h"
#include "elf.h"
#include "aspace.h"
#include "paging.h"
#include "pmm.h"
#include "kprintf.h"
#include "vga.h"
#include "string.h"
#include "gdt.h"

extern struct { u32 prev, esp0; } tss;          /* debut de la structure de gdt.c */
extern int  user_enter(void *ctx, u32 entry, u32 user_esp, u32 *esp0_slot);
extern void user_leave(void *ctx, int code);

static struct uproc proc;
static int user_ok;
static const u8 *pending_in;
static u32 pending_in_len;
static int pending_in_set;
static struct user_result *current_result;

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

struct uproc *user_current(void)
{
    return proc.running ? &proc : NULL;
}

void user_set_stdin(const u8 *buf, u32 len, int set)
{
    pending_in = buf;
    pending_in_len = len;
    pending_in_set = set;
}

/* Construit la pile initiale : argc, argv[], NULL, puis les chaines. Renvoie ESP. */
static err_t build_stack(u8 *mem, int argc, char **argv, u32 *esp)
{
    u32 top = USER_END, total = 0, sp, argv_addr;
    u32 ptrs[USER_ARGS_MAX];

    if (argc < 0 || argc > USER_ARGS_MAX)
        return E_2BIG;
    for (int i = 0; i < argc; i++) {
        total += (u32)strlen(argv[i]) + 1;
        if (total > USER_ARGS_BYTES)
            return E_2BIG;
    }
    sp = top;
    for (int i = argc - 1; i >= 0; i--) {
        u32 len = (u32)strlen(argv[i]) + 1;

        sp -= len;
        memcpy(mem + (sp - USER_BASE), argv[i], len);
        ptrs[i] = sp;
    }
    sp &= ~15u;
    sp -= 4 * ((u32)argc + 1);                  /* argv[0..argc-1] et NULL */
    argv_addr = sp;
    for (int i = 0; i < argc; i++)
        memcpy(mem + (argv_addr - USER_BASE) + 4u * (u32)i, &ptrs[i], 4);
    memset(mem + (argv_addr - USER_BASE) + 4u * (u32)argc, 0, 4);
    sp -= 4;
    memcpy(mem + (sp - USER_BASE), &argc, 4);
    *esp = sp;
    return E_OK;
}

_Static_assert(USER_BASE == ASPACE_BASE && USER_END == ASPACE_END, "zone des programmes = une table de pages");

static struct aspace space;                     /* espace d'adressage du programme en cours */

/* Rend la main au noyau (son repertoire de pages) et libere les pages du programme. */
static void space_release(void)
{
    aspace_activate_kernel();
    aspace_destroy(&space);
}

err_t user_exec(const u8 *image, u32 size, const char *name, int argc, char **argv,
                struct user_result *res)
{
    struct elf_seg segs[ELF_MAX_SEGS];
    int nseg = 0;
    u32 entry, esp;
    u32 stack_lo = USER_END - USER_STACK_SIZE;
    err_t e;

    if (!user_ok || !paging_enabled())
        return E_NOSYS;
    if (proc.running)
        return E_BUSY;

    e = elf_segments(image, size, USER_BASE, stack_lo, segs, &nseg, &entry);   /* valide avant de rien reserver */
    if (e != E_OK)
        return e;
    e = aspace_create(&space);
    if (e != E_OK)
        return e;
    for (int i = 0; i < nseg && e == E_OK; i++)
        e = aspace_map(&space, segs[i].lo, segs[i].hi, 1);          /* en ecriture le temps du chargement */
    if (e == E_OK)
        e = aspace_map(&space, stack_lo, USER_END, 1);
    if (e != E_OK) {
        aspace_destroy(&space);
        return e;
    }

    aspace_activate(&space);                    /* a partir d'ici le noyau voit la memoire DU programme en 4-8 Mio */
    e = elf_load(image, size, (u8 *)USER_BASE, USER_BASE, stack_lo, &entry);
    if (e == E_OK)
        e = build_stack((u8 *)USER_BASE, argc, argv, &esp);
    if (e != E_OK) {
        space_release();
        return e;
    }
    for (int i = 0; i < nseg; i++)              /* code et constantes : lecture seule (W^X partiel) */
        if (!segs[i].writable)
            aspace_protect(&space, segs[i].lo, segs[i].hi, 0);
    aspace_activate(&space);                    /* recharge CR3 : vide le cache des traductions */

    user_proc_reset(&proc, name);
    proc.as = &space;
    proc.in = pending_in;
    proc.in_len = pending_in_len;
    proc.in_set = pending_in_set;
    pending_in_set = 0;
    proc.running = 1;

    res->exit_code = 0;
    res->killed = 0;
    current_result = res;

    user_enter(&proc.ctx, entry, esp, &tss.esp0);

    proc.running = 0;
    proc.as = NULL;
    current_result = NULL;
    res->exit_code = proc.exit_code;
    space_release();
    return E_OK;
}

/* Termine le programme en cours et rend la main a user_exec. Ne revient pas. */
static void user_finish(void)
{
    user_leave(&proc.ctx, 1);
    for (;;)
        ;
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
    if (p->exit_requested) {
        if (p->exit_code == 130 && current_result)
            current_result->killed = 1;
        user_finish();
    }
}

void user_fault(struct regs *r, const char *what)
{
    struct uproc *p = user_current();

    if (!p) {
        return;                                 /* l'appelant retombera sur la panique */
    }
    vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
    keprintf("\n%s : programme arrete - %s (exception %u, EIP=%#x)\n", p->name, what, r->int_no, r->eip);
    if (r->int_no == 14) {
        u32 cr2;

        __asm__ volatile ("movl %%cr2, %0" : "=r"(cr2));
        keprintf("  adresse fautive : %#x (%s)\n", cr2,
                 (r->err_code & 1) ? "acces interdit : memoire du noyau ou page protegee"
                                   : "page absente");
    }
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    p->exit_code = 128 + (int)r->int_no;
    if (current_result)
        current_result->killed = 1;
    user_finish();
}
