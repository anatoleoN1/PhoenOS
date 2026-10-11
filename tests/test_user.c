/*
 * Test du VRAI user.c, syscall.c, sched.c et aspace.c ensemble (0.7.0) : programmes qui en lancent d'autres.
 *
 * Seul le passage en ring 3 est simule : enter_user_mode() est remplace par une fonction C qui "joue" un
 * programme en faisant de vrais appels systeme (syscall_entry). Tout le reste est le code du noyau :
 * creation des espaces d'adressage, taches avec leurs piles (vraies commutations), spawn, waitpid, kill, brk, exit.
 */
#include "testlib.h"
#include "io.h"
#include "multiboot.h"
#include "pmm.h"
#include "kheap.h"
#include "fs.h"

#define enter_user_mode test_enter_user_mode       /* user.c appellera NOTRE version */
#include "user.c"

u8 test_scancode;
char kernel_start[4096] __attribute__((aligned(4096)));
char kernel_end[1];
void test_panic(const char *fmt, ...) { out("  PANIQUE INATTENDUE : "); out(fmt); out("\n"); exit_now(2); }

static char fake_ram[16u << 20] __attribute__((aligned(4096)));
static u32 kdir[1024] __attribute__((aligned(4096)));
u32 test_tss_store[4] __asm__("tss");                  /* le "tss" de gdt.c : user.c n'ecrit que esp0 */

/* ---- materiel simule ---- */
static u32 ticks;
u32 pit_ticks(void) { return ticks; }
u32 pit_hz(void) { return 100; }
u32 pit_uptime_ms(void) { return ticks * 10; }
void pit_sleep_ms(u32 ms) { (void)ms; }
static u32 idle_calls;
static void idle(void)
{
    ticks++;
    if (++idle_calls > 100000) { out("  BOUCLE SANS FIN (aucune tache ne repart)\n"); exit_now(2); }
}
void vga_set_color(enum vga_color fg, enum vga_color bg) { (void)fg; (void)bg; }
static char screen[8192];
static usize slen_;
void vga_putc(char c) { if (slen_ + 1 < sizeof screen) screen[slen_++] = c; }
void serial_putc(char c) { (void)c; }
int (*keyboard_wait_hook)(void);
int keyboard_getc(void) { return -1; }
int keyboard_peek(void) { return -1; }
int keyboard_try_getc(void) { return -1; }
void aspace_activate(const struct aspace *a) { (void)a; }
void aspace_activate_kernel(void) { }
int paging_enabled(void) { return 1; }
static int shows(const char *sub) { usize n = strlen(sub); for (usize i = 0; i + n <= slen_; i++) if (!memcmp(screen + i, sub, n)) return 1; return 0; }

/* ---- memoire "des programmes" : le noyau de test lit les pointeurs directement, il faut donc une vraie zone a 4 Mio ---- */
static long mmap_fixed(u32 addr, u32 len)
{
    long r;

    __asm__ volatile ("pushl %%ebp\n\tmovl $0, %%ebp\n\tint $0x80\n\tpopl %%ebp"
                      : "=a"(r) : "0"(192), "b"(addr), "c"(len), "d"(3), "S"(0x32), "D"(-1) : "memory", "cc");
    return r;
}
#define UM(off) ((char *)(USER_BASE + (off)))
#define UA(off) ((u32)(USER_BASE + (off)))

static i32 sc(u32 nr, u32 a, u32 b, u32 c)
{
    struct regs r;

    memset(&r, 0, sizeof r);
    r.cs = 0x1B;
    r.eax = nr; r.ebx = a; r.ecx = b; r.edx = c;
    syscall_entry(&r);
    return (i32)r.eax;
}

/* ---- les programmes simules ---- */
enum { K_EXIT7, K_YIELD3, K_SLEEP100, K_SPAWNWAIT, K_ORPHAN, K_FOREVER, K_SLEEP30, K_SLEEP60, K_WAITANY, K_NOHANG,
       K_ERRORS, K_BRK, K_MANY, K_SELFKILL, K_BRKFULL, K_TREE, K_LIB, K_COUNT };

static struct {
    i32 kid, w, code, again;                            /* spawnwait */
    i32 orphan_kid;
    i32 pids[4], codes[4], w4;                          /* waitany */
    i32 nh, nh_w, nh_kid;                               /* nohang */
    i32 e[24];                                          /* errors */
    i32 ppid_of_kid, kid_ppid;
    i32 brk[12]; u32 pages[6]; int checks[8];
    i32 many_spawned, many_err, many_reaped;
    i32 yields;
    i32 tree_kid, tree_kid2, tree_w;
    i32 lib[10]; u32 lib_pages, lib_nlibs; int lib_ro, lib_rw;
} R;

static void prog(int k)
{
    switch (k) {
    case K_EXIT7: sc(SYS_EXIT, 7, 0, 0); break;
    case K_YIELD3: for (int i = 0; i < 3; i++) { sc(SYS_SCHED_YIELD, 0, 0, 0); R.yields++; } sc(SYS_EXIT, 3, 0, 0); break;
    case K_SLEEP100: sc(SYS_SLEEP_MS, 100, 0, 0); sc(SYS_EXIT, 5, 0, 0); break;
    case K_SLEEP30: sc(SYS_SLEEP_MS, 30, 0, 0); sc(SYS_EXIT, 6, 0, 0); break;
    case K_SLEEP60: sc(SYS_SLEEP_MS, 60, 0, 0); sc(SYS_EXIT, 8, 0, 0); break;
    case K_FOREVER: for (;;) sc(SYS_SCHED_YIELD, 0, 0, 0);
    case K_SPAWNWAIT:
        strcpy(UM(0x100), "/bin/k0");
        R.kid = sc(SYS_SPAWN, UA(0x100), 0, 0);
        R.kid_ppid = (i32)user_current()->pid;
        R.ppid_of_kid = (i32)sched_find((u32)R.kid)->parent;
        R.w = sc(SYS_WAITPID, (u32)R.kid, UA(0x200), 0);
        R.code = *(int *)UM(0x200);
        R.again = sc(SYS_WAITPID, (u32)R.kid, 0, 0);            /* deja libere : plus un enfant */
        sc(SYS_EXIT, 0, 0, 0);
        break;
    case K_ORPHAN:
        strcpy(UM(0x100), "/bin/k2");
        R.orphan_kid = sc(SYS_SPAWN, UA(0x100), 0, 0);
        sc(SYS_EXIT, 0, 0, 0);                                  /* part sans attendre son enfant */
        break;
    case K_WAITANY:
        strcpy(UM(0x100), "/bin/k2"); strcpy(UM(0x110), "/bin/k6"); strcpy(UM(0x120), "/bin/k7");
        R.pids[0] = sc(SYS_SPAWN, UA(0x100), 0, 0);
        R.pids[1] = sc(SYS_SPAWN, UA(0x110), 0, 0);
        R.pids[2] = sc(SYS_SPAWN, UA(0x120), 0, 0);
        for (int i = 0; i < 3; i++) {
            R.e[i] = sc(SYS_WAITPID, (u32)-1, UA(0x200), 0);     /* dans l'ordre de FIN */
            R.codes[i] = *(int *)UM(0x200);
        }
        R.w4 = sc(SYS_WAITPID, (u32)-1, 0, 0);
        sc(SYS_EXIT, 0, 0, 0);
        break;
    case K_NOHANG:
        strcpy(UM(0x100), "/bin/k2");
        R.nh_kid = sc(SYS_SPAWN, UA(0x100), 0, 0);
        R.nh = sc(SYS_WAITPID, (u32)R.nh_kid, 0, WNOHANG);       /* tourne encore : 0 */
        R.nh_w = sc(SYS_WAITPID, (u32)R.nh_kid, UA(0x200), 0);
        R.code = *(int *)UM(0x200);
        sc(SYS_EXIT, 0, 0, 0);
        break;
    case K_ERRORS: {
        u32 av;
        char *s;

        strcpy(UM(0x100), "/bin/nope"); strcpy(UM(0x120), "/bin"); strcpy(UM(0x140), "/f.txt"); strcpy(UM(0x160), "k0");
        R.e[0] = sc(SYS_WAITPID, 9999, 0, 0);                    /* n'existe pas */
        R.e[1] = sc(SYS_WAITPID, 0, 0, 0);                       /* 0 : invalide */
        R.e[2] = sc(SYS_WAITPID, (u32)-1, 0, 0);                 /* pas d'enfant du tout */
        R.e[3] = sc(SYS_WAITPID, (u32)-1, 0x10, 0);              /* statut hors zone */
        R.e[4] = sc(SYS_WAITPID, 9999, 0, 2);                    /* option inconnue */
        R.e[5] = sc(SYS_SPAWN, UA(0x100), 0, 0);                 /* fichier absent */
        R.e[6] = sc(SYS_SPAWN, 0x10, 0, 0);                      /* pointeur invalide */
        R.e[7] = sc(SYS_SPAWN, UA(0x120), 0, 0);                 /* un dossier */
        R.e[8] = sc(SYS_SPAWN, UA(0x140), 0, 0);                 /* pas un ELF */
        /* argv : 17 mots = trop */
        s = UM(0x300);
        for (int i = 0; i < 17; i++) { s[0] = 'a'; s[1] = 0; memcpy((void *)UM(0x400 + 4 * i), &(u32){ UA(0x300) }, 4); }
        memset(UM(0x400 + 4 * 17), 0, 4);
        R.e[9] = sc(SYS_SPAWN, UA(0x160), UA(0x400), 0);
        memcpy((void *)UM(0x500), &(u32){ 0x10 }, 4);            /* argv[0] : pointeur invalide */
        R.e[10] = sc(SYS_SPAWN, UA(0x160), UA(0x500), 0);
        R.e[11] = sc(SYS_SPAWN, UA(0x160), 0x10, 0);             /* argv lui-meme invalide */
        /* argv[0] sans fin dans la zone : trop long (4096 octets max) */
        memset(UM(0x600), 'x', 0x2000);
        memcpy((void *)UM(0x500), &(u32){ UA(0x600) }, 4); memset(UM(0x504), 0, 4);
        R.e[12] = sc(SYS_SPAWN, UA(0x160), UA(0x500), 0);
        R.e[13] = sc(SYS_KILL, 9999, 15, 0);
        R.e[14] = sc(SYS_KILL, 0, 15, 0);                        /* le shell */
        R.e[15] = sc(SYS_KILL, 9999, 99, 0);                     /* signal hors limites */
        /* "k0" sans / : cherche dans /bin ; argv = deux mots */
        memcpy((void *)UM(0x500), &(u32){ UA(0x160) }, 4); memcpy((void *)UM(0x504), &(u32){ UA(0x300) }, 4); memset(UM(0x508), 0, 4);
        av = UA(0x500);
        R.e[16] = sc(SYS_SPAWN, UA(0x160), av, 0);
        R.e[17] = sc(SYS_WAITPID, (u32)R.e[16], UA(0x200), 0);
        R.code = *(int *)UM(0x200);
        R.e[18] = sc(SYS_GETPPID, 0, 0, 0);
        sc(SYS_EXIT, 0, 0, 0);
        break;
    }
    case K_BRK: {
        struct uproc *p = user_current();
        u32 lo = (u32)sc(SYS_BRK, 0, 0, 0), hi = p->heap_hi;

        R.brk[0] = (i32)lo; R.pages[0] = p->as->pages;
        R.brk[1] = sc(SYS_BRK, lo + 10000, 0, 0); R.pages[1] = p->as->pages;
        R.checks[0] = aspace_check(p->as, lo, 10000, 1);
        R.checks[1] = aspace_check(p->as, lo + 12288, 1, 0);      /* 4e page : pas mappee */
        memset(UM(lo - USER_BASE), 0x5A, 4);                     /* (zone de test du noyau) */
        R.brk[2] = sc(SYS_BRK, lo + 100, 0, 0); R.pages[2] = p->as->pages;       /* reduit : rend les pages */
        R.checks[2] = aspace_check(p->as, lo + 4096, 1, 0);
        R.brk[3] = sc(SYS_BRK, hi + 1, 0, 0);                    /* au-dela de la limite : refuse */
        R.brk[4] = sc(SYS_BRK, lo - 1, 0, 0);                    /* avant le debut : refuse */
        R.brk[5] = sc(SYS_BRK, 0, 0, 0);
        R.brk[6] = sc(SYS_BRK, hi, 0, 0); R.pages[3] = p->as->pages;             /* tout le tas possible */
        R.checks[3] = aspace_check(p->as, hi - 1, 1, 1);
        R.checks[4] = aspace_check(p->as, hi, 1, 0);             /* la page de garde reste absente */
        R.brk[7] = sc(SYS_BRK, lo, 0, 0); R.pages[4] = p->as->pages;             /* tout rendu */
        R.brk[8] = (i32)hi;
        sc(SYS_EXIT, 0, 0, 0);
        break;
    }
    case K_BRKFULL: {
        u32 lo = (u32)sc(SYS_BRK, 0, 0, 0);

        R.brk[9] = sc(SYS_BRK, lo + (1u << 20), 0, 0);           /* la memoire est epuisee : refuse */
        R.brk[10] = (i32)lo;
        R.pages[5] = user_current()->as->pages;
        sc(SYS_EXIT, 0, 0, 0);
        break;
    }
    case K_MANY: {
        int n = 0, e;
        u32 first = 0;

        strcpy(UM(0x100), "/bin/k0");
        for (;;) {
            e = sc(SYS_SPAWN, UA(0x100), 0, 0);
            if (e < 0)
                break;
            if (!first)
                first = (u32)e;
            n++;
        }
        R.many_spawned = n; R.many_err = e;
        while (sc(SYS_WAITPID, (u32)-1, 0, 0) > 0)
            R.many_reaped++;
        sc(SYS_EXIT, 0, 0, 0);
        break;
    }
    case K_TREE:
        strcpy(UM(0x100), "/bin/k5");                            /* petit-enfant : ne s'arrete jamais */
        R.tree_kid = sc(SYS_SPAWN, UA(0x100), 0, 0);
        strcpy(UM(0x110), "/bin/k8");                            /* enfant qui lui-meme lance trois enfants */
        R.tree_kid2 = sc(SYS_SPAWN, UA(0x110), 0, 0);
        R.w = sc(SYS_WAITPID, (u32)R.tree_kid, 0, 0);           /* attend pour toujours : seul kill peut l'arreter */
        break;
    case K_LIB: {
        struct uproc *p = user_current();

        strcpy(UM(0x100), "maths"); strcpy(UM(0x110), "maths_pgcd"); strcpy(UM(0x120), "inconnu");
        strcpy(UM(0x130), "absente"); strcpy(UM(0x140), "texte"); strcpy(UM(0x150), "texte_mots");
        R.lib_nlibs = p->nlibs; R.lib_pages = p->as->pages;
        R.lib_ro = aspace_check(p->as, 0x700000, 16, 0) && !aspace_check(p->as, 0x700000, 1, 1);
        R.lib_rw = aspace_check(p->as, 0x701000, 4, 1);
        R.lib[0] = sc(SYS_PSL, UA(0x100), 0, 0);                    /* base de maths (deja chargee ou chargee a la demande) */
        R.lib[1] = sc(SYS_PSL, UA(0x100), UA(0x110), 0);
        R.lib[2] = sc(SYS_PSL, UA(0x100), UA(0x120), 0);            /* symbole inconnu */
        R.lib[3] = sc(SYS_PSL, UA(0x130), 0, 0);                    /* bibliotheque absente */
        R.lib[4] = sc(SYS_PSL, 0x10, 0, 0);                         /* nom : pointeur invalide */
        R.lib[5] = sc(SYS_PSL, UA(0x100), 0x10, 0);                 /* symbole : pointeur invalide */
        R.lib[6] = sc(SYS_PSL, UA(0x140), UA(0x150), 0);            /* texte : peut etre chargee a la demande */
        R.lib[7] = (i32)p->nlibs;
        sc(SYS_EXIT, 0, 0, 0);
        break;
    }
    case K_SELFKILL:
        sc(SYS_KILL, (u32)sc(SYS_GETPID, 0, 0, 0), 9, 0);
        R.yields = 99;                                           /* ne doit jamais s'executer */
        break;
    default: break;
    }
    sc(SYS_EXIT, 0, 0, 0);
}

void test_enter_user_mode(u32 entry, u32 user_esp)
{
    (void)user_esp;
    prog((int)(entry - USER_BASE));
    for (;;) ;
}

/* ---- ELF forge : un segment lecture/ecriture de 3 pages a 4 Mio, point d'entree = 4 Mio + numero du programme ---- */
struct eh { u8 id[16]; u16 type, machine; u32 version, entry, phoff, shoff, flags; u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx; } __attribute__((packed));
struct ph { u32 type, offset, vaddr, paddr, filesz, memsz, flags, align; } __attribute__((packed));
static u8 img[4096 + 256];

static void forge(u32 entry)
{
    struct eh h;
    struct ph p;

    memset(img, 0, sizeof img);
    memset(&h, 0, sizeof h);
    h.id[0] = 0x7F; h.id[1] = 'E'; h.id[2] = 'L'; h.id[3] = 'F'; h.id[4] = 1; h.id[5] = 1; h.id[6] = 1;
    h.type = 2; h.machine = 3; h.version = 1; h.entry = entry; h.phoff = 52; h.ehsize = 52; h.phentsize = 32; h.phnum = 1;
    memcpy(img, &h, sizeof h);
    memset(&p, 0, sizeof p);
    p.type = 1; p.offset = 4096; p.vaddr = USER_BASE; p.paddr = USER_BASE; p.filesz = 256; p.memsz = 0x3000; p.flags = 6;
    memcpy(img + 52, &p, sizeof p);
}

/* Lance le programme k depuis le "shell" (en arriere-plan) et attend sa fin. Renvoie son numero. */
static u32 run_k(int k, int *code)
{
    char name[8] = "k0";
    char *av[1];
    struct fs_node *n;
    u32 pid = 0;
    int known = 0;
    err_t e;

    name[1] = (char)('0' + k % 10);
    if (k >= 10) { name[1] = (char)('0' + k / 10); name[2] = (char)('0' + k % 10); name[3] = 0; }
    av[0] = name;
    {
        char path[16] = "/bin/";

        strcpy(path + 5, name);
        if (fs_lookup(path, &n) != E_OK)
            return 0;
    }
    e = user_spawn_bg(n->data, n->size, name, 1, av, &pid);
    if (e != E_OK)
        return 0;
    e = user_wait_pid(pid, code, &known);
    if (e != E_OK || !known)
        return 0;
    return pid;
}

static void settle(void) { for (int i = 0; i < 20; i++) sched_yield(); }

int test_main(void)
{
    static struct { struct multiboot_info mbi; struct multiboot_mmap_entry e[1]; } M;
    int code = -1;
    u32 pid, base_frames, k;

    CHECK(mmap_fixed(USER_BASE, USER_SIZE) == (long)USER_BASE);
    M.mbi.flags = MB_FLAG_MMAP;
    M.mbi.mmap_addr = (u32)M.e;
    M.mbi.mmap_length = sizeof M.e;
    M.e[0] = (struct multiboot_mmap_entry){ 20, (u32)fake_ram, 0, sizeof fake_ram, 0, MB_MMAP_AVAILABLE };
    pmm_init(&M.mbi);
    kheap_init();
    fs_init();
    fs_mkdir("/bin");
    for (k = 0; k < K_COUNT; k++) {
        struct fs_node *f;
        char path[16] = "/bin/k";

        path[6] = (char)('0' + (k >= 10 ? k / 10 : k));
        if (k >= 10) { path[7] = (char)('0' + k % 10); path[8] = 0; } else path[7] = 0;
        forge(USER_BASE + k);
        CHECK(fs_create(path, &f) == E_OK && fs_write(f, img, sizeof img) == E_OK);
    }
    { struct fs_node *f; fs_create("/f.txt", &f); fs_write(f, "texte\n", 6); }
    aspace_set_kernel_dir(kdir);
    sched_init();
    sched_idle_hook = idle;
    user_sched_init();
    user_ok = 1;
    CHECK(user_available());

    /* ---- un programme simple : code de sortie recupere par le shell (wait) ---- */
    pid = run_k(K_EXIT7, &code);
    CHECK(pid >= 1 && code == 7);
    settle();
    CHECK(sched_find(pid) == NULL);                             /* detache : libere tout seul */
    code = -1;
    CHECK(user_wait_pid(pid, &code, &(int){0}) == E_OK && code == 7);   /* le code reste connu apres la liberation */
    CHECK(user_wait_pid(0, &code, &(int){0}) == E_INVAL);       /* le shell n'est pas un programme */
    CHECK(user_wait_pid(9999, &code, &(int){0}) == E_NOENT);

    /* ---- echauffement : le cas le plus gourmand, pour que le tas du noyau ait fini de grandir ---- */
    CHECK(run_k(K_MANY, &code) && code == 0);
    settle();
    base_frames = pmm_free_frames();

    /* ---- un parent lance un enfant et l'attend ---- */
    CHECK(run_k(K_SPAWNWAIT, &code) && code == 0);
    CHECK(R.kid > 0 && R.w == R.kid && R.code == 7);            /* waitpid renvoie le numero, et le code de l'enfant */
    CHECK(R.ppid_of_kid == R.kid_ppid && R.kid_ppid > 0);       /* l'enfant sait qui est son parent */
    CHECK(R.again == E_CHILD);                                  /* un enfant ne s'attend qu'une fois */
    settle();
    CHECK(sched_find((u32)R.kid) == NULL && pmm_free_frames() == base_frames);   /* rien ne fuit */

    /* ---- les enfants sont a nous seuls : un parent qui s'en va laisse des orphelins qui se liberent seuls ---- */
    CHECK(run_k(K_ORPHAN, &code) && code == 0);
    CHECK(R.orphan_kid > 0);
    {
        struct task *t = sched_find((u32)R.orphan_kid);
        int known = 0;

        CHECK(t && t->detached && t->parent == 0);              /* adopte par personne : libere a sa fin */
        CHECK(user_wait_pid((u32)R.orphan_kid, &code, &known) == E_OK && known && code == 5);
    }
    settle();
    CHECK(sched_find((u32)R.orphan_kid) == NULL && pmm_free_frames() == base_frames);

    /* ---- waitpid(-1) : les enfants sont rendus dans l'ordre de leur FIN (30 ms, 60 ms, 100 ms) ---- */
    CHECK(run_k(K_WAITANY, &code) && code == 0);
    CHECK(R.e[0] == R.pids[1] && R.codes[0] == 6);              /* k6 (30 ms) d'abord */
    CHECK(R.e[1] == R.pids[2] && R.codes[1] == 8);              /* puis k7 (60 ms) */
    CHECK(R.e[2] == R.pids[0] && R.codes[2] == 5);              /* puis k2 (100 ms) */
    CHECK(R.w4 == E_CHILD);                                     /* plus d'enfant */
    settle();
    CHECK(pmm_free_frames() == base_frames);

    /* ---- WNOHANG ---- */
    CHECK(run_k(K_NOHANG, &code) && code == 0);
    CHECK(R.nh == 0 && R.nh_w == R.nh_kid && R.code == 5);
    settle();
    CHECK(pmm_free_frames() == base_frames);

    /* ---- erreurs ---- */
    CHECK(run_k(K_ERRORS, &code) && code == 0);
    CHECK(R.e[0] == E_CHILD && R.e[1] == E_INVAL && R.e[2] == E_CHILD && R.e[3] == E_FAULT && R.e[4] == E_INVAL);
    CHECK(R.e[5] == E_NOENT && R.e[6] == E_FAULT && R.e[7] == E_ISDIR && R.e[8] == E_NOEXEC);
    CHECK(R.e[9] == E_2BIG && R.e[10] == E_FAULT && R.e[11] == E_FAULT && R.e[12] == E_2BIG);
    CHECK(R.e[13] == E_NOENT && R.e[14] == E_INVAL && R.e[15] == E_INVAL);
    CHECK(R.e[16] > 0 && R.e[17] == R.e[16] && R.code == 7);    /* "k0" sans / : trouve dans /bin */
    CHECK(R.e[18] == 0);                                        /* lance par le shell : parent 0 */
    settle();
    CHECK(pmm_free_frames() == base_frames);

    /* ---- yield ---- */
    CHECK(run_k(K_YIELD3, &code) && code == 3 && R.yields == 3);

    /* ---- kill : arret d'un programme qui ne s'arrete jamais ; auto-arret ---- */
    {
        u32 fpid = 0;
        char *av[1] = { "k5" };
        struct fs_node *n;
        int known = 0;

        CHECK(fs_lookup("/bin/k5", &n) == E_OK && user_spawn_bg(n->data, n->size, "k5", 1, av, &fpid) == E_OK);
        sched_yield(); sched_yield();
        CHECK(sched_find(fpid) != NULL);
        CHECK(user_kill(fpid) == E_OK);
        CHECK(user_wait_pid(fpid, &code, &known) == E_OK && known && code == 130);
        CHECK(user_kill(fpid) == E_NOENT || user_kill(fpid) == E_INVAL);
        CHECK(user_kill(0) == E_INVAL && user_kill(9999) == E_NOENT);
    }
    code = -1;
    CHECK(run_k(K_SELFKILL, &code) && code == 130 && R.yields != 99);
    settle();
    CHECK(pmm_free_frames() == base_frames);

    /* ---- arret d'un arbre : le parent, son enfant sans fin et son enfant qui a lui-meme des enfants ---- */
    {
        u32 tp = 0;
        char *av[1] = { "k15" };
        struct fs_node *n;
        int known = 0;

        CHECK(fs_lookup("/bin/k15", &n) == E_OK && user_spawn_bg(n->data, n->size, "k15", 1, av, &tp) == E_OK);
        for (int i = 0; i < 6; i++) sched_yield();
        CHECK(sched_find((u32)R.tree_kid) && sched_find((u32)R.tree_kid2));
        CHECK(user_kill_tree(tp) == E_OK);
        CHECK(user_wait_pid(tp, &code, &known) == E_OK && known && code == 130);
        settle();
        CHECK(sched_find((u32)R.tree_kid) == NULL && sched_find((u32)R.tree_kid2) == NULL);   /* personne ne reste en route */
        CHECK(user_kill_tree(tp) == E_NOENT && user_kill_tree(0) == E_INVAL);
    }
    settle();
    CHECK(pmm_free_frames() == base_frames);

    /* ---- tas : brk ---- */
    CHECK(run_k(K_BRK, &code) && code == 0);
    {
        u32 lo = (u32)R.brk[0], hi = (u32)R.brk[8];

        CHECK(lo == 0x403000 && hi == 0x6FF000);                /* apres le segment ; limite = pile (0x7F0000) - 1 page de garde */
        CHECK((u32)R.brk[1] == lo + 10000 && R.pages[1] == R.pages[0] + 3 && R.checks[0] && !R.checks[1]);
        CHECK((u32)R.brk[2] == lo + 100 && R.pages[2] == R.pages[0] + 1 && !R.checks[2]);    /* pages rendues */
        CHECK((u32)R.brk[3] == lo + 100 && (u32)R.brk[4] == lo + 100 && (u32)R.brk[5] == lo + 100);   /* refus : inchange */
        CHECK((u32)R.brk[6] == hi && R.pages[3] == R.pages[0] + (hi - lo) / 4096 && R.checks[3] && !R.checks[4]);
        CHECK((u32)R.brk[7] == lo && R.pages[4] == R.pages[0]);
    }
    settle();
    CHECK(pmm_free_frames() == base_frames);

    /* ---- memoire epuisee : brk refuse proprement et ne change rien ---- */
    {
        u32 hog[4096], nh = 0, f;

        while (pmm_free_frames() > 30 && nh < 4096 && (f = pmm_alloc_frame()) != 0)
            hog[nh++] = f;
        CHECK(run_k(K_BRKFULL, &code) && code == 0);
        CHECK(R.brk[9] == R.brk[10] && R.pages[5] == 19);       /* 3 pages de segment + 16 de pile, rien de plus */
        while (nh)
            pmm_free_frame(hog[--nh]);
    }
    settle();
    CHECK(pmm_free_frames() == base_frames);

    /* ---- table pleine : spawn refuse (E_BUSY), tout se libere ensuite ---- */
    R.many_reaped = 0;
    CHECK(run_k(K_MANY, &code) && code == 0);
    CHECK(R.many_spawned == SCHED_MAX_TASKS - 2 && R.many_err == E_BUSY && R.many_reaped == R.many_spawned);
    settle();
    CHECK(pmm_free_frames() == base_frames);
    {
        struct task_info ti;
        int n = 0;

        while (sched_info(n, &ti))
            n++;
        CHECK(n == 1);                                          /* il ne reste que le shell */
    }

    /* ---- les programmes lances avec & annoncent leur fin sur l'ecran ; les enfants d'un programme, non ---- */
    CHECK(shows("termine (code 7)") && shows("k6") == 0);

    /* ---- bibliotheques .psl (0.8.0) : donnees par l'en-tete .poe (user_set_needs) ou a la demande (appel psl) ---- */
    {
        struct fs_node *f;
        static u8 lf[1 << 17];
        const char *names[2] = { "maths", "texte" };
        static char needs[2][POE_LIBNAME_MAX];
        u32 before, k2;

        fs_mkdir("/lib");
        for (k2 = 0; k2 < 2; k2++) {
            char host[24] = "lib/", path[24] = "/lib/";
            long r;
            int fd;

            strcpy(host + 4, names[k2]); strcpy(host + 4 + strlen(names[k2]), ".psl");
            strcpy(path + 5, names[k2]); strcpy(path + 5 + strlen(names[k2]), ".psl");
            fd = (int)sys3(5, (long)host, 0, 0);
            r = fd < 0 ? -1 : sys3(3, fd, (long)lf, sizeof lf);
            CHECK(r > 0 && fs_create(path, &f) == E_OK && fs_write(f, lf, (u32)r) == E_OK);
            sys3(6, fd, 0, 0);
        }
        settle();
        before = pmm_free_frames();
        /* a la demande : le programme appelle psl() */
        memset(&R.lib, 0, sizeof R.lib);
        CHECK(run_k(K_LIB, &code) && code == 0);
        CHECK(R.lib_nlibs == 0 && !R.lib_ro);                                   /* rien au depart */
        CHECK(R.lib[0] == 0x700000 && R.lib[1] == 0x700000 && R.lib[2] == E_NOENT && R.lib[3] == E_NOLIB);
        CHECK(R.lib[4] == E_FAULT && R.lib[5] == E_FAULT && (u32)R.lib[6] >= 0x710000 && (u32)R.lib[6] < 0x712000 && R.lib[7] == 2);
        settle();
        CHECK(pmm_free_frames() == before);                                      /* rendu a la fin du programme */
        { const char *nm; u32 a, b, c2, d; CHECK(libmgr_info(0, &nm, &a, &b, &c2, &d) == 0); }
        /* donnees par l'en-tete : deja la des le premier instruction du programme */
        strcpy(needs[0], "maths"); strcpy(needs[1], "texte");
        user_set_needs(needs, 2);
        memset(&R.lib, 0, sizeof R.lib);
        CHECK(run_k(K_LIB, &code) && code == 0);
        CHECK(R.lib_nlibs == 2 && R.lib_ro && R.lib_rw);
        CHECK(R.lib[0] == 0x700000 && (u32)R.lib[6] >= 0x710000 && (u32)R.lib[6] < 0x712000 && R.lib[7] == 2);   /* deja chargees : meme resultat, rien en plus */
        settle();
        CHECK(pmm_free_frames() == before);
        /* bibliotheque absente : le lancement echoue proprement, rien ne fuit, les "needs" sont consommes */
        strcpy(needs[0], "maths"); strcpy(needs[1], "absente");
        user_set_needs(needs, 2);
        CHECK(run_k(K_LIB, &code) == 0);
        settle();
        CHECK(pmm_free_frames() == before);
        { const char *nm; u32 a, b, c2, d; CHECK(libmgr_info(0, &nm, &a, &b, &c2, &d) == 0); }
        memset(&R.lib, 0, sizeof R.lib);
        CHECK(run_k(K_LIB, &code) && code == 0 && R.lib_nlibs == 0);            /* le lancement suivant n'herite de rien */
        settle();
        CHECK(pmm_free_frames() == before);
    }

    out("  programmes qui en lancent d autres + bibliotheques .psl verifies : spawn, waitpid (pid, -1, WNOHANG), orphelins, kill, brk, erreurs, table pleine, fuites\n");
    return 0;
}
