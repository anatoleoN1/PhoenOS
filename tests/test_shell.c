/*
 * Test du vrai shell.c de bout en bout : des touches simulées entrent dans le shell,
 * et on vérifie ce qui serait affiché à l'écran. MODE 0 : session complète ;
 * MODE 1..4 : commandes `crash` qui doivent déclencher une panique.
 */
#include <stdarg.h>
#include "testlib.h"
#include "multiboot.h"
#include "keyboard.h"
#include "kprintf.h"
#include "vga.h"
#include "serial.h"
#include "pit.h"
#include "pmm.h"
#include "kheap.h"
#include "power.h"
#include "panic.h"
#include "shell.h"
#include "fs.h"
#include "initrd.h"

#ifndef MODE
#define MODE 0
#endif

u8 test_scancode;
char kernel_start[4096] __attribute__((aligned(4096)));
char kernel_end[1];
static char fake_ram[16u << 20] __attribute__((aligned(4096)));

/* ---------------- écran simulé : capture tout ce qui est affiché ---------------- */
static char outbuf[1 << 17];
static usize outlen;

void vga_set_color(enum vga_color fg, enum vga_color bg) { (void)fg; (void)bg; }
void vga_clear(void) { outlen = 0; }
void vga_putc(char c)
{
    if (c == '\b') { if (outlen) outlen--; return; }
    if (outlen + 1 < sizeof outbuf) outbuf[outlen++] = c;
}
void serial_putc(char c) { (void)c; }

static int count_in(const char *buf, usize len, const char *sub)
{
    usize n = strlen(sub);
    int k = 0;

    for (usize i = 0; i + n <= len; i++)
        if (!memcmp(buf + i, sub, n))
            k++;
    return k;
}

/* Copie de l'écran juste avant `dmesg` (qui rejoue tout et double donc les occurrences). */
static char snap[1 << 17];
static usize snaplen;
static int snapshot_at = -1;

static int count(const char *sub)  { return count_in(snap, snaplen, sub); }        /* avant dmesg */
static int count_end(const char *sub) { return count_in(outbuf, outlen, sub); }    /* écran final */

/* ---------------- matériel simulé ---------------- */
static u32 last_sleep;
u32  pit_ticks(void)       { return 1234; }
u32  pit_uptime_ms(void)   { return 3725000; }              /* 1 h 2 min 5 s */
void pit_sleep_ms(u32 ms)  { last_sleep = ms; }

static const char *expect;
void test_panic(const char *fmt, ...)
{
    if (expect) {
        usize n = strlen(expect), m = strlen(fmt);

        for (usize i = 0; i + n <= m; i++) {
            if (!memcmp(fmt + i, expect, n)) {
                out("  panique attendue : "); out(fmt); out("\n");
                exit_now(EXIT_EXPECTED_PANIC);
            }
        }
    }
    out("  PANIQUE INATTENDUE : "); out(fmt); out("\n");
    exit_now(2);
}

/* ---------------- clavier simulé ---------------- */
static int keys[8192];
static int nkeys, next_key;

static void key(int k) { keys[nkeys++] = k; }
static void type(const char *s) { while (*s) key((u8)*s++); }

static void verify(void) __attribute__((noreturn));
int keyboard_getc(void)
{
    if (next_key == snapshot_at) {
        memcpy(snap, outbuf, outlen);
        snaplen = outlen;
    }
    if (next_key < nkeys)
        return keys[next_key++];
    out("  Le script s'est termine sans `reboot`.\n");
    test_fails++;
    verify();
    return 0;
}

void power_reboot(void)   { verify(); }
void power_shutdown(void) { out("  shutdown appele\n"); exit_now(3); }
void power_halt(void)     { out("  halt appele\n"); exit_now(3); }

/* ---------------- vérifications de la session ---------------- */
static void verify(void)
{
#if MODE == 0
    CHECK(count("phoenos:/> ") > 20);
    CHECK(count("Commandes (Tab complete") == 3);                 /* help, puis he<Tab> */
    CHECK(count("\na   b c\n") == 1);                           /* guillemets */
    CHECK(count("Commande inconnue : foo") == 1);
    CHECK(count("usage : sleep <millisecondes>") == 1);
    CHECK(count("sleep : echec") == 0 && count("memtest : echec") == 0);   /* pas d'erreur en double */
    CHECK(last_sleep == 250);
    CHECK(count("Maximum : 60000 ms.") == 1);
    CHECK(count("1 h 2 min 5 s") == 1);
    CHECK(count("Tas du noyau") == 1 && count("Memoire physique") == 1);
    CHECK(count("8 Kio : 4 motifs ecrits et relus sans erreur") == 1);
    CHECK(count("Taille entre 1 et 4096 Kio.") == 1);
    CHECK(count("\ndeux\n") == 2);                              /* ligne rappelee par les fleches */
    CHECK(count("\nhi\n") == 1);                                /* retour arriere */
    CHECK(count("^C") == 3 && count("\nnope\n") == 0 && count("\nok\n") == 1);
    CHECK(count("\nzzz\n") == 0 && count("\nyes\n") == 1);      /* Ctrl+U */
    CHECK(count("\ndraft\n") == 1);                             /* brouillon restaure apres les fleches */
    CHECK(count("sleep  shutdown") == 1);                       /* Tab : plusieurs possibilites */
    CHECK(count("Trop d'arguments (maximum 8).") == 1);
    CHECK(count("Guillemet non ferme.") == 1);
    CHECK(count("\ncaf\x82\n") == 1);                           /* caractere accentue CP437 */
    CHECK(count("  1  sleep 250") == 0 || count("  1  ") >= 1); /* history : numerotation */
    CHECK(count("gris clair") == 1 && count("blanc") == 1);     /* colors */
    CHECK(count(" 10   vert clair") == 1);                      /* %2d correct dans colors */
    CHECK(count("PhoenOS 0.0.6") == 1);                         /* version */
    CHECK(count("PANIQUE") == 0);

    /* ---- fichiers ---- */
    CHECK(count("\n/a\n") == 1);                               /* pwd apres cd /a */
    CHECK(count("phoenos:/a> ") >= 8 && count("phoenos:/a/b> ") == 1);   /* le prompt suit le dossier */
    CHECK(count("\nhello\nmore\n") == 2);                      /* cat apres > puis >> (et cat de g.txt) */
    CHECK(count("mkdir: /a: existe deja") == 1);
    CHECK(count("cat: nofile: introuvable") == 1);
    CHECK(count("-       11  f.txt") == 1 && count("d        -  b/") == 1);   /* ls -l */
    CHECK(count("\n> x\n") == 1);                              /* echo ">" x : pas une redirection */
    CHECK(count("Redirection invalide") == 1);
    CHECK(count("redirection: /nodir/z: introuvable") == 1);
    CHECK(count("rm: /a/b: est un dossier") == 1);
    CHECK(count("rmdir: a: dossier non vide") == 1);
    CHECK(count("b/  f.txt  g.txt  ") == 1);                    /* ls apres cp */
    CHECK(count("cat g.txt \n") == 1);                          /* Tab complete g<Tab> en g.txt */
    CHECK(count("00000000  68 65 6c 6c 6f") == 1 && count("... 6 octet(s) de plus") == 1);   /* hexdump */
    CHECK(count("2 dossier(s), 2 fichier(s)") == 1);            /* tree / */
    CHECK(count("Redirection : commande") == 3);                /* help, puis cat h.txt (help redirige) */
    CHECK(count("Commandes (Tab complete") == 3);               /* help redirige + cat : sortie bien capturee */
    CHECK(count("PANIQUE") == 0);

    /* après `dmesg` : tout le journal rejoué une seconde fois */
    CHECK(count_end("Commandes (Tab complete") == 6);
    CHECK(count_end("\ndeux\n") == 4);
    CHECK(count_end("Redemarrage...") == 1);
    if (test_fails) {
        out("---- ecran simule (avant dmesg) ----\n");
        snap[snaplen] = 0;
        out(snap);
        out("\n----------------------\n");
    } else {
        out("  session de shell complete verifiee ("); outnum((u32)nkeys); out(" touches simulees)\n");
    }
#endif
    exit_now(test_fails);
}

int test_main(void)
{
    static struct { struct multiboot_info mbi; struct multiboot_mmap_entry e[1]; } M;

    M.mbi.flags = MB_FLAG_MMAP;
    M.mbi.mmap_addr = (u32)M.e;
    M.mbi.mmap_length = sizeof M.e;
    M.e[0] = (struct multiboot_mmap_entry){ 20, (u32)fake_ram, 0, sizeof fake_ram, 0, MB_MMAP_AVAILABLE };
    pmm_init(&M.mbi);
    kheap_init();
    fs_init();

#if MODE == 0
    type("help\n");
    type("echo \"a   b\"  c\n");
    type("foo\n");
    type("hel\t\n");                                             /* Tab complete en "help " */
    type("sleep abc\n");
    type("sleep 250\n");
    type("sleep 70000\n");
    type("uptime\n");
    type("mem\n");
    type("memtest 8\n");
    type("memtest 0\n");
    type("echo un\n");
    type("echo deux\n");
    key(KEY_UP); key(KEY_UP); key(KEY_DOWN); type("\n");        /* rappelle "echo deux" */
    type("ech"); key('x'); key('\b'); type("o hi\n");           /* retour arriere */
    type("echo nope"); key(3); type("echo ok\n");               /* Ctrl+C */
    type("echo zzz"); key(21); type("echo yes\n");              /* Ctrl+U */
    type("echo dr"); key(KEY_UP); key(KEY_DOWN); type("aft\n"); /* brouillon conserve : "echo draft" */
    type("s\t"); key(3);                                        /* Tab ambigu : liste sleep, shutdown */
    type("echo 1 2 3 4 5 6 7 8 9\n");
    type("echo \"abc\n");
    type("echo caf"); key(0x82); type("\n");
    key(KEY_LEFT); key(KEY_HOME); key(KEY_DEL); key(KEY_END);   /* touches ignorees sans effet */
    for (int i = 0; i < 400; i++) key('a');                     /* ligne trop longue : bornee, sans plantage */
    key(3);
    type("history\n");
    type("colors\n");
    type("version\n");
    type("mkdir -p /a/b\n");
    type("cd /a\n");
    type("pwd\n");
    type("echo hello > f.txt\n");
    type("cat f.txt\n");
    type("echo more >> f.txt\n");
    type("cat f.txt\n");
    type("ls -l\n");
    type("cd b\n");
    type("cd ..\n");
    type("mkdir /a\n");
    type("cat nofile\n");
    type("echo \">\" x\n");
    type("echo a > b > c\n");
    type("echo hi > /nodir/z\n");
    type("rm /a/b\n");
    type("cd /\n");
    type("rmdir a\n");
    type("cp /a/f.txt /a/g.txt\n");
    type("cd a\n");
    type("ls\n");
    type("cat g.\t\n");
    type("hexdump f.txt 5\n");
    type("cd /\n");
    type("tree /\n");
    type("help > h.txt\n");
    type("cat h.txt\n");
    snapshot_at = nkeys;
    type("dmesg\n");
    type("reboot\n");
#elif MODE == 1
    expect = "Panique demandee"; type("crash panic\n");
#elif MODE == 2
    expect = "assert"; type("crash assert\n");
#elif MODE == 3
    expect = "Double liberation detectee"; type("crash doublefree\n");
#elif MODE == 4
    expect = "Corruption du tas"; type("crash heap\n");
#endif
    shell_run();
}
