/* Test du vrai syscall.c : appels systeme avec un faux programme (memoire = tableau du test). */
#include "testlib.h"
#include "user.h"
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
static struct uproc P;
#define A(x) ((u32)(umem + (x)))
#define SYS(nr, a, b, c) syscall_dispatch(&P, (nr), (a), (b), (c))

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
    SYS(SYS_SLEEP_MS, 250, 0, 0);
    CHECK(slept == 250);
    SYS(SYS_SLEEP_MS, 0xFFFFFFFFu, 0, 0);
    CHECK(slept == 60000);                                                  /* borne */
    CHECK(SYS(SYS_TIME, A(600), 0, 0) == 0 && SYS(SYS_TIME, 0, 0, 0) == 0); /* horloge illisible dans ce test */
    CHECK(SYS(SYS_TIME, 0x1000, 0, 0) == E_FAULT);
    CHECK(SYS(999, 0, 0, 0) == E_NOSYS);
    CHECK(SYS(0, 0, 0, 0) == E_NOSYS);

    /* ---- exit ---- */
    CHECK(!P.exit_requested);
    CHECK(SYS(SYS_EXIT, 0x1234, 0, 0) == 0 && P.exit_requested && P.exit_code == 0x34);

    out("  appels systeme verifies : write/read/open/close/unlink, ecriture de fichiers, pointeurs hostiles, clavier, tube, exit\n");
    return 0;
}
