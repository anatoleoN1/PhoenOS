/* Test du vrai syscall.c : appels systeme avec un faux programme (memoire = tableau du test). */
#include "testlib.h"
#include "user.h"
#include "dlog.h"
#include "aspace.h"
#include "fs.h"
#include "kprintf.h"
#include "keyboard.h"
#include "pit.h"
#include "pmm.h"
#include "kheap.h"
#include "multiboot.h"
#include "vga.h"
#include "serial.h"

u8 test_scancode;
char kernel_start[4096] __attribute__((aligned(4096)));
char kernel_end[1];
static char fake_ram[4u << 20] __attribute__((aligned(4096)));
static u8 umem[8192] __attribute__((aligned(4096)));            /* "memoire du programme" */

static char screen[4096];
static usize slen_;
void vga_set_color(enum vga_color fg, enum vga_color bg) { (void)fg; (void)bg; }
void vga_putc(char c) { if (slen_ + 1 < sizeof screen) screen[slen_++] = c; }
void serial_putc(char c) { (void)c; }
u32 pit_ticks(void) { return 0; }
u32 pit_uptime_ms(void) { return 4242; }
static u32 slept;
static int yielded;
void pit_sleep_ms(u32 ms) { slept = ms; }
void test_panic(const char *fmt, ...) { out("  PANIQUE INATTENDUE : "); out(fmt); out("\n"); exit_now(2); }

static int keys[64], nk, ik;
int keyboard_getc(void)
{
    if (ik < nk)
        return keys[ik++];
    out("  clavier epuise\n");
    exit_now(2);
}
static void type(const char *s) { while (*s) keys[nk++] = (u8)*s++; }

static int shows(const char *sub) { usize n = strlen(sub); for (usize i = 0; i + n <= slen_; i++) if (!memcmp(screen + i, sub, n)) return 1; return 0; }

enum { O_WRONLY = 1, O_RDWR = 2, O_CREAT = 0x40, O_TRUNC = 0x200, O_APPEND = 0x400 };
void aspace_activate(const struct aspace *a) { (void)a; }
void aspace_activate_kernel(void) { }
static u32 kdir[1024] __attribute__((aligned(4096)));
static struct uproc P;

static int hook_begin, hook_end;
static char hook_last;
static void test_hook(int phase, char last)
{
    if (phase == 0) hook_begin++;
    else { hook_end++; hook_last = last; }
}

static int strstr_(const char *h, const char *n)
{
    for (; *h; h++) {
        usize i = 0;

        while (n[i] && h[i] == n[i])
            i++;
        if (!n[i])
            return 1;
    }
    return 0;
}
#define A(x) ((u32)(umem + (x)))
#define SYS(nr, a, b, c) syscall_dispatch(&P, (nr), (a), (b), (c))

int  sched_ready(void)       { return 0; }
void sched_sleep_ms(u32 ms)  { (void)ms; }
void sched_yield(void)       { yielded++; }
struct task;
err_t sched_create(const char *name, void (*entry)(void *), void *arg, void (*cleanup)(void *), void *owner, struct task **out)
{ (void)name; (void)entry; (void)arg; (void)cleanup; (void)owner; (void)out; return E_OK; }
void sched_detach(struct task *t) { (void)t; }

/* 0.7.0 : le vrai user.c est teste dans test_user.c ; ici on verifie seulement ce que syscall.c transmet */
static u32 sp_path_len; static int sp_argc; static char sp_arg0[32], sp_arg1[32];
err_t user_spawn_child(struct uproc *p, const char *path, int argc, char **argv, u32 *pid)
{
    (void)p;
    sp_path_len = (u32)strlen(path);
    sp_argc = argc;
    strcpy(sp_arg0, argc > 0 ? argv[0] : "");
    strcpy(sp_arg1, argc > 1 ? argv[1] : "");
    *pid = 42;
    return strcmp(path, "/refuse") ? E_OK : E_BUSY;
}
static i32 wp_pid; static u32 wp_opt;
i32 user_waitpid(struct uproc *p, i32 pid, u32 options, int *code)
{
    (void)p;
    wp_pid = pid; wp_opt = options;
    *code = 77;
    return pid > 0 ? pid : 5;
}
static u32 killed_pid;
err_t user_kill(u32 pid) { killed_pid = pid; return pid == 9 ? E_NOENT : E_OK; }

int test_main(void)
{
    static struct { struct multiboot_info mbi; struct multiboot_mmap_entry e[1]; } M;
    struct fs_node *f;

    M.mbi.flags = MB_FLAG_MMAP;
    M.mbi.mmap_addr = (u32)M.e;
    M.mbi.mmap_length = sizeof M.e;
    M.e[0] = (struct multiboot_mmap_entry){ 20, (u32)fake_ram, 0, sizeof fake_ram, 0, MB_MMAP_AVAILABLE };
    pmm_init(&M.mbi);
    kheap_init();
    fs_init();
    fs_create("/f.txt", &f);
    fs_write(f, "abc\ndef\n", 8);
    fs_mkdir("/d");

    user_proc_reset(&P, "test");
    P.win_lo = (u32)umem;
    P.win_hi = (u32)umem + sizeof umem;
    CHECK(P.pid >= 1 && !strcmp(P.name, "test"));

    /* ---- write ---- */
    memcpy(umem, "salut\n", 6);
    CHECK(SYS(SYS_WRITE, 1, A(0), 6) == 6 && shows("salut\n"));
    CHECK(SYS(SYS_WRITE, 2, A(0), 6) == 6);
    CHECK(SYS(SYS_WRITE, 5, A(0), 6) == E_INVAL);                          /* fd inconnu */
    CHECK(SYS(SYS_WRITE, 0, A(0), 6) == E_INVAL);                          /* entree standard */
    CHECK(SYS(SYS_WRITE, 1, A(0), 0) == 0);
    CHECK(SYS(SYS_WRITE, 1, A(sizeof umem - 2), 6) == E_FAULT);             /* depasse la zone */
    CHECK(SYS(SYS_WRITE, 1, (u32)umem - 1, 2) == E_FAULT);                  /* commence avant */
    CHECK(SYS(SYS_WRITE, 1, 0, 4) == E_FAULT);                              /* adresse 0 */
    CHECK(SYS(SYS_WRITE, 1, 0x100000u, 4) == E_FAULT);                      /* memoire du noyau */
    CHECK(SYS(SYS_WRITE, 1, 0xFFFFFFFCu, 100) == E_FAULT);                  /* debordement */
    CHECK(SYS(SYS_WRITE, 1, A(0), 0xFFFFFFFFu) == E_FAULT);                 /* taille enorme */

    /* ---- open / read / close ---- */
    strcpy((char *)umem + 100, "/f.txt");
    strcpy((char *)umem + 120, "/nofile");
    strcpy((char *)umem + 140, "/d");
    CHECK(SYS(SYS_OPEN, A(100), 0, 0) == 3);
    CHECK(SYS(SYS_OPEN, A(100), 0, 0) == 4);                                /* deuxieme descripteur */
    CHECK(SYS(SYS_OPEN, A(120), 0, 0) == E_NOENT);
    CHECK(SYS(SYS_OPEN, A(140), 0, 0) == E_ISDIR);
    CHECK(SYS(SYS_OPEN, A(100), 0x8000, 0) == E_INVAL);                     /* option inconnue */
    CHECK(SYS(SYS_OPEN, A(100), 3, 0) == E_INVAL);                          /* mode d'acces invalide */
    CHECK(SYS(SYS_OPEN, A(100), 0x40, 0) == E_INVAL);                       /* creer sans ecriture */
    CHECK(SYS(SYS_OPEN, 0, 0, 0) == E_FAULT);
    memset(umem + 200, 'x', 300);                                           /* chaine sans fin dans la zone */
    CHECK(SYS(SYS_OPEN, A(200), 0, 0) == E_FAULT);                          /* trop longue : refusee */
    memset(umem + 8192 - 4, 'y', 4);
    CHECK(SYS(SYS_OPEN, A(8192 - 4), 0, 0) == E_FAULT);                     /* sort de la zone avant le 0 */
    CHECK(SYS(SYS_READ, 3, A(300), 5) == 5 && !memcmp(umem + 300, "abc\nd", 5));
    CHECK(SYS(SYS_READ, 3, A(300), 100) == 3 && !memcmp(umem + 300, "ef\n", 3));   /* le reste */
    CHECK(SYS(SYS_READ, 3, A(300), 100) == 0);                              /* fin de fichier */
    CHECK(SYS(SYS_READ, 4, A(300), 2) == 2 && umem[300] == 'a');            /* position propre a chaque fd */
    CHECK(SYS(SYS_READ, 3, A(8190), 10) == E_FAULT);
    CHECK(SYS(SYS_READ, 9, A(300), 2) == E_INVAL);
    CHECK(SYS(SYS_CLOSE, 3, 0, 0) == 0 && SYS(SYS_CLOSE, 3, 0, 0) == E_INVAL);
    CHECK(SYS(SYS_READ, 3, A(300), 2) == E_INVAL);                          /* ferme */
    CHECK(SYS(SYS_OPEN, A(100), 0, 0) == 3);                                /* le numero est reutilise */
    for (int i = 0; i < 20; i++)
        SYS(SYS_OPEN, A(100), 0, 0);
    CHECK(SYS(SYS_OPEN, A(100), 0, 0) == E_NOMEM);                          /* table pleine */

    /* ---- ecriture de fichiers (0.4.0) ---- */
    for (int i = 3; i < 3 + USER_MAX_FDS; i++)
        SYS(SYS_CLOSE, (u32)i, 0, 0);
    strcpy((char *)umem + 1000, "/neuf.txt");
    strcpy((char *)umem + 1020, "/d/sous.txt");
    strcpy((char *)umem + 1040, "/rien/x.txt");
    CHECK(SYS(SYS_OPEN, A(1000), O_WRONLY, 0) == E_NOENT);                  /* n'existe pas, pas de O_CREAT */
    CHECK(SYS(SYS_OPEN, A(1040), O_WRONLY | O_CREAT, 0) == E_NOENT);        /* dossier parent absent */
    CHECK(SYS(SYS_OPEN, A(1000), O_WRONLY | O_CREAT, 0) == 3);
    memcpy(umem + 1100, "un\ndeux\n", 8);
    CHECK(SYS(SYS_WRITE, 3, A(1100), 8) == 8);
    CHECK(SYS(SYS_READ, 3, A(1200), 4) == E_INVAL);                         /* ouvert en ecriture seule */
    CHECK(SYS(SYS_WRITE, 3, A(8190), 10) == E_FAULT);
    /* connexions TCP (0.10.0) : pointeurs hostiles et arguments invalides, sans reseau */
    CHECK(SYS(SYS_SOCK_CONNECT, 0, 80, 0) == E_FAULT);                      /* nom a l'adresse 0 */
    CHECK(SYS(SYS_SOCK_CONNECT, A(0), 0, 0) == E_INVAL);                    /* port 0 */
    CHECK(SYS(SYS_SOCK_CONNECT, A(0), 70000, 0) == E_INVAL);                /* port trop grand */
    CHECK(SYS(SYS_SOCK_SEND, 0, 0, 10) == E_FAULT);                         /* tampon a l'adresse 0 */
    CHECK(SYS(SYS_SOCK_SEND, 0, A(0), 10) == E_INVAL);                      /* pas de connexion 0 */
    CHECK(SYS(SYS_SOCK_RECV, 0, 0x100000u, 4) == E_FAULT);                  /* memoire du noyau */
    CHECK(SYS(SYS_SOCK_RECV, 0, A(0), 0) == 0);                             /* rien a recevoir */
    CHECK(SYS(SYS_SOCK_CLOSE, 9, 0, 0) == E_INVAL);                         /* numero hors table */
    CHECK(SYS(SYS_CLOSE, 3, 0, 0) == 0);
    CHECK(fs_lookup("/neuf.txt", &f) == E_OK && f->size == 8 && !memcmp(f->data, "un\ndeux\n", 8));
    CHECK(SYS(SYS_OPEN, A(1000), O_WRONLY | O_APPEND, 0) == 3);             /* ajoute a la suite */
    CHECK(SYS(SYS_WRITE, 3, A(1100), 2) == 2 && f->size == 10 && !memcmp(f->data + 8, "un", 2));
    CHECK(SYS(SYS_CLOSE, 3, 0, 0) == 0);
    CHECK(SYS(SYS_OPEN, A(1000), O_RDWR | O_TRUNC, 0) == 3 && f->size == 0);   /* vide le fichier */
    CHECK(SYS(SYS_WRITE, 3, A(1100), 3) == 3);
    CHECK(SYS(SYS_READ, 3, A(1200), 10) == 0);                              /* lecture a la position courante */
    CHECK(SYS(SYS_CLOSE, 3, 0, 0) == 0);
    CHECK(SYS(SYS_OPEN, A(1000), 0, 0) == 3);
    CHECK(SYS(SYS_WRITE, 3, A(1100), 1) == E_INVAL);                        /* lecture seule : ecriture refusee */
    CHECK(SYS(SYS_READ, 3, A(1200), 10) == 3 && !memcmp(umem + 1200, "un\n", 3));
    CHECK(SYS(SYS_OPEN, A(1000), O_WRONLY, 0) == 4);                        /* ouvert 2 fois : meme fichier */
    CHECK(SYS(SYS_UNLINK, A(1000), 0, 0) == 0);                             /* ferme 3 et 4 */
    CHECK(fs_lookup("/neuf.txt", &f) == E_NOENT);
    CHECK(SYS(SYS_READ, 3, A(1200), 1) == E_INVAL && SYS(SYS_WRITE, 4, A(1100), 1) == E_INVAL);
    CHECK(SYS(SYS_UNLINK, A(1000), 0, 0) == E_NOENT);
    CHECK(SYS(SYS_UNLINK, A(140), 0, 0) == E_ISDIR);                        /* /d : un dossier */
    CHECK(SYS(SYS_UNLINK, 0, 0, 0) == E_FAULT);
    CHECK(SYS(SYS_OPEN, A(1020), O_WRONLY | O_CREAT, 0) == 3);              /* creation dans un sous-dossier */
    CHECK(fs_lookup("/d/sous.txt", &f) == E_OK && f->type == FS_FILE);
    CHECK(SYS(SYS_CLOSE, 3, 0, 0) == 0);
    CHECK(SYS(SYS_OPEN, A(140), O_WRONLY, 0) == E_ISDIR);                   /* pas d'ecriture dans un dossier */
    {
        u32 big = 0;

        CHECK(SYS(SYS_OPEN, A(1020), O_WRONLY | O_APPEND, 0) == 3);
        while (big < (1u << 20) && SYS(SYS_WRITE, 3, A(0), 4096) == 4096)
            big += 4096;
        i32 r = SYS(SYS_WRITE, 3, A(0), 4096);

        CHECK(big > 0 && (r == E_FBIG || r == E_NOMEM));                    /* refus propre quand c'est plein */
        CHECK(fs_lookup("/d/sous.txt", &f) == E_OK && f->size == big);      /* rien d'ecrit a moitie */
        SYS(SYS_CLOSE, 3, 0, 0);
        fs_remove("/d/sous.txt", 0);
    }

    /* ---- pages reellement mappees (0.5.0) : un pointeur dans la fenetre mais sur une page absente est refuse ---- */
    {
        struct aspace S;
        struct uproc Q;

        aspace_set_kernel_dir(kdir);
        CHECK(aspace_create(&S) == E_OK);
        CHECK(aspace_map(&S, 0x400000, 0x401000, 1) == E_OK && aspace_map(&S, 0x500000, 0x501000, 1) == E_OK);
        CHECK(aspace_protect(&S, 0x500000, 0x501000, 0) == E_OK);
        user_proc_reset(&Q, "espace");
        Q.as = &S;                                                              /* fenetre par defaut : 4-8 Mio */
        CHECK(syscall_dispatch(&Q, SYS_WRITE, 1, 0x600000, 4) == E_FAULT);      /* dans la fenetre, mais page absente */
        CHECK(syscall_dispatch(&Q, SYS_WRITE, 1, 0x400FFE, 4) == E_FAULT);      /* commence sur une page mappee, finit sur une absente */
        CHECK(syscall_dispatch(&Q, SYS_WRITE, 1, 0x400000, 0) == 0);            /* longueur nulle sur page mappee */
        CHECK(syscall_dispatch(&Q, SYS_READ, 0, 0x600000, 4) == E_FAULT);
        CHECK(syscall_dispatch(&Q, SYS_READ, 0, 0x500000, 4) == E_FAULT);       /* le noyau n'ecrit pas dans une page en lecture seule */
        CHECK(syscall_dispatch(&Q, SYS_READ, 0, 0x500000, 0) == 0);
        CHECK(syscall_dispatch(&Q, SYS_OPEN, 0x600000, 0, 0) == E_FAULT);          /* chemin sur une page absente */
        CHECK(syscall_dispatch(&Q, SYS_UNLINK, 0x600000, 0, 0) == E_FAULT);
        CHECK(syscall_dispatch(&Q, SYS_TIME, 0x600000, 0, 0) == E_FAULT);
        CHECK(syscall_dispatch(&Q, SYS_TIME, 0x500000, 0, 0) == E_FAULT);          /* time(&t) vers une page en lecture seule */
        CHECK(syscall_dispatch(&Q, SYS_WRITE, 1, 0x3FFFF0, 4) == E_FAULT);      /* avant la fenetre */
        aspace_destroy(&S);
    }

    /* ---- entree standard : tube ---- */
    P.in = (const u8 *)"tube\nsuite";
    P.in_len = 10; P.in_pos = 0; P.in_set = 1;
    CHECK(SYS(SYS_READ, 0, A(400), 4) == 4 && !memcmp(umem + 400, "tube", 4));
    CHECK(SYS(SYS_READ, 0, A(400), 100) == 6 && !memcmp(umem + 400, "\nsuite", 6));
    CHECK(SYS(SYS_READ, 0, A(400), 100) == 0);

    /* ---- entree standard : clavier (ligne, echo, retour arriere) ---- */
    P.in_set = 0;
    slen_ = 0;
    type("Abx"); keys[nk++] = '\b'; type("c\n");
    CHECK(SYS(SYS_READ, 0, A(500), 100) == 4 && !memcmp(umem + 500, "Abc\n", 4));
    CHECK(shows("Abx") && shows("c\n"));
    type("xy\n");
    CHECK(SYS(SYS_READ, 0, A(500), 1) == 1 && umem[500] == 'x');            /* lecture en plusieurs fois */
    CHECK(SYS(SYS_READ, 0, A(500), 10) == 2 && !memcmp(umem + 500, "y\n", 2));
    keys[nk++] = 4;                                                         /* Ctrl+D sur ligne vide */
    CHECK(SYS(SYS_READ, 0, A(500), 10) == 0 && !P.exit_requested);
    keys[nk++] = 3;                                                         /* Ctrl+C */
    CHECK(SYS(SYS_READ, 0, A(500), 10) == 0 && P.exit_requested && P.exit_code == 130 && shows("^C"));
    P.exit_requested = 0; P.exit_code = 0;
    type("ab"); keys[nk++] = KEY_UP; type("\n");                            /* touches speciales ignorees */
    CHECK(SYS(SYS_READ, 0, A(500), 10) == 3 && !memcmp(umem + 500, "ab\n", 3));

    /* ---- divers ---- */
    CHECK(SYS(SYS_GETPID, 0, 0, 0) == (i32)P.pid);
    CHECK(SYS(SYS_UPTIME, 0, 0, 0) == 4242);
    /* ---- 0.6.3 : le shell est prevenu avant/apres chaque ecriture d'un programme sur la console ---- */
    memcpy(umem + 800, "hop\n", 4);
    kprintf_task_hook = test_hook;
    CHECK(SYS(SYS_WRITE, 1, A(800), 4) == 4);
    CHECK(hook_begin == 1 && hook_end == 1 && hook_last == '\n');
    CHECK(SYS(SYS_WRITE, 1, A(800), 3) == 3 && hook_last == 'p');
    CHECK(SYS(SYS_WRITE, 1, A(800), 0) == 0 && hook_begin == 2);               /* longueur nulle : pas d'appel */
    kprintf_task_hook = NULL;

    /* ---- journal : log(niveau, texte) ---- */
    {
        char tag[DLOG_TAG_MAX], msg[DLOG_MSG_MAX + 2];
        u32 ms, n0 = dlog_total();
        int lv, ok = 0;

        memcpy(umem + 700, "bonjour", 8);
        dlog_set_level(DL_TRACE);
        CHECK(SYS(SYS_LOG, 2, A(700), 0) == 0);
        CHECK(dlog_total() > n0);
        for (u32 i = 0; dlog_get(i, &ms, &lv, tag, msg); i++)
            if (lv == DL_INFO && strcmp(tag, P.name) == 0 && strstr_(msg, "bonjour"))
                ok = 1;
        CHECK(ok);
        CHECK(SYS(SYS_LOG, 9, A(700), 0) == E_INVAL);                       /* niveau inconnu */
        CHECK(SYS(SYS_LOG, 2, 0x600000, 0) == E_FAULT);                     /* pointeur invalide */
    }
    SYS(SYS_SLEEP_MS, 250, 0, 0);
    CHECK(slept == 250);
    SYS(SYS_SLEEP_MS, 0xFFFFFFFFu, 0, 0);
    CHECK(slept == 60000);                                                  /* borne */
    CHECK(SYS(SYS_TIME, A(600), 0, 0) == 0 && SYS(SYS_TIME, 0, 0, 0) == 0); /* horloge illisible dans ce test */
    CHECK(SYS(SYS_TIME, 0x1000, 0, 0) == E_FAULT);
    CHECK(SYS(999, 0, 0, 0) == E_NOSYS);
    CHECK(SYS(0, 0, 0, 0) == E_NOSYS);

    /* ---- 0.8.0 : psl(nom, symbole) ---- */
    strcpy((char *)umem + 2500, "maths"); strcpy((char *)umem + 2520, "maths_pgcd");
    CHECK(SYS(SYS_PSL, 0, 0, 0) == E_FAULT && SYS(SYS_PSL, A(2500), 0x100000, 0) == E_FAULT);    /* pointeurs invalides */
    CHECK(SYS(SYS_PSL, A(2500), A(2520), 0) == E_NOSYS);                    /* sans espace d'adressage : pas de bibliotheques */
    memset((char *)umem + 2600, 'x', 40);
    CHECK(SYS(SYS_PSL, A(2600), 0, 0) == E_FAULT);                          /* nom sans fin dans 19 octets */

    /* ---- 0.7.0 : spawn, waitpid, kill, getppid, yield (le noyau valide les pointeurs puis transmet) ---- */
    strcpy((char *)umem + 2000, "/bin/x"); strcpy((char *)umem + 2020, "arg-un"); strcpy((char *)umem + 2040, "arg-deux");
    { u32 av[3] = { A(2020), A(2040), 0 }; memcpy(umem + 2100, av, sizeof av); }
    CHECK(SYS(SYS_SPAWN, A(2000), A(2100), 0) == 42 && sp_argc == 2 && !strcmp(sp_arg0, "arg-un") && !strcmp(sp_arg1, "arg-deux"));
    CHECK(SYS(SYS_SPAWN, A(2000), 0, 0) == 42 && sp_argc == 1 && !strcmp(sp_arg0, "/bin/x") && sp_path_len == 6);   /* sans argv : le chemin */
    { u32 av0[1] = { 0 }; memcpy(umem + 2200, av0, 4); }
    CHECK(SYS(SYS_SPAWN, A(2000), A(2200), 0) == 42 && sp_argc == 1 && !strcmp(sp_arg0, "/bin/x"));              /* argv vide : le chemin aussi */
    CHECK(SYS(SYS_SPAWN, 0, 0, 0) == E_FAULT && SYS(SYS_SPAWN, A(2000), 0x100000, 0) == E_FAULT);
    strcpy((char *)umem + 2300, "/refuse");
    CHECK(SYS(SYS_SPAWN, A(2300), 0, 0) == E_BUSY);                                   /* l'erreur du noyau est rendue telle quelle */
    CHECK(SYS(SYS_WAITPID, 7, A(2400), 0) == 7 && wp_pid == 7 && wp_opt == 0 && *(int *)(umem + 2400) == 77);
    CHECK(SYS(SYS_WAITPID, (u32)-1, 0, WNOHANG) == 5 && wp_pid == -1 && wp_opt == WNOHANG);   /* statut optionnel */
    CHECK(SYS(SYS_WAITPID, 7, 0x100000, 0) == E_FAULT);                               /* statut hors zone : refuse AVANT d'attendre */
    CHECK(SYS(SYS_WAITPID, 7, A(8190), 0) == E_FAULT);
    CHECK(SYS(SYS_KILL, 5, 15, 0) == 0 && killed_pid == 5);
    CHECK(SYS(SYS_KILL, 9, 15, 0) == E_NOENT && SYS(SYS_KILL, 5, 32, 0) == E_INVAL);
    CHECK(SYS(SYS_GETPPID, 0, 0, 0) == (i32)P.ppid);
    CHECK(SYS(SYS_SCHED_YIELD, 0, 0, 0) == 0 && yielded == 0);                        /* ordonnanceur inactif ici : sans effet */
    CHECK(SYS(SYS_BRK, 0, 0, 0) == 0);                                                 /* pas de tas dans ce test */
    P.exit_requested = 0;
    CHECK(SYS(SYS_KILL, P.pid, 9, 0) == 0 && P.exit_requested && P.exit_code == 130);  /* se tuer soi-meme = s'arreter */
    P.exit_requested = 0; P.exit_code = 0;

    /* ---- exit ---- */
    CHECK(!P.exit_requested);
    CHECK(SYS(SYS_EXIT, 0x1234, 0, 0) == 0 && P.exit_requested && P.exit_code == 0x34);

    out("  appels systeme verifies : write/read/open/close/unlink, ecriture de fichiers, pointeurs hostiles, clavier, tube, exit\n");
    return 0;
}
