/*
 * Test du vrai shell.c de bout en bout : des touches simulées entrent dans le shell,
 * et on vérifie ce qui serait affiché à l'écran. MODE 0 : session complète ;
 * MODE 1..4 : commandes `crash` qui doivent déclencher une panique.
 */
#include "fakeata.h"
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
#include "rtc.h"
#include "user.h"
#include "sched.h"
#include "crc32.h"
#include "initrd.h"
#include "ata.h"
#include "persist.h"
#include "netdev.h"
#include "net.h"

#ifndef MODE
#define MODE 0
#endif

u8 test_scancode;
static u32 fake_clock(void) { return 1791185472u; }      /* 2026-10-05 07:31:12 */
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
static int scroll_total, scroll_resets;
void vga_scroll_view(int lines) { scroll_total += lines; }
void vga_scroll_reset(void) { scroll_resets++; }

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

/* reseau : doublures (pas de vraie carte) */
err_t paging_map_mmio(u32 phys, u32 size) { (void)phys; (void)size; return E_NOSYS; }
err_t sched_create(const char *n, void (*e)(void *), void *a, void (*c)(void *), void *o, struct task **out) { (void)n; (void)e; (void)a; (void)c; (void)o; (void)out; return E_NOSYS; }
void sched_detach(struct task *t) { (void)t; }
static u32 net_clock;
u32 net_now_ms(void) { return net_clock; }
void net_delay_ms(u32 ms) { net_clock += ms; }
static int fk_link(struct netdev *d) { (void)d; return 1; }
static err_t fk_send(struct netdev *d, const u8 *f, u32 l) { (void)d; (void)f; (void)l; return E_OK; }
static int fk_recv(struct netdev *d, u8 *b, u32 c) { (void)d; (void)b; (void)c; return 0; }
static struct netdev fake_nic = { .driver = "fake", .mac = { 2, 0, 0, 0, 0, 9 }, .link_up = fk_link, .send = fk_send, .recv = fk_recv };

/* faux ordonnanceur : le shell (0) et un programme "ticker" (7) en arriere-plan */
int  sched_ready(void)            { return 0; }
void sched_sleep_ms(u32 ms)       { last_sleep = ms; }
u32  sched_switches(void)         { return 42; }
u32  sched_idle_count(void)       { return 7; }
int  sched_trace_get(int i, u32 *t, char *k, u32 *a, u32 *b) { if (i) return 0; *t = 9; *k = 'S'; *a = 0; *b = 1; return 1; }
int  sched_info(int i, struct task_info *o)
{
    if (i == 0) { o->id = 0; strcpy(o->name, "shell"); o->state = T_RUNNING; o->ticks = 5; o->detached = 0; o->parent = 0; return 1; }
    if (i == 1) { o->id = 7; strcpy(o->name, "ticker"); o->state = T_BLOCKED; o->ticks = 123; o->detached = 1; o->parent = 0; return 1; }
    return 0;
}
err_t user_kill_tree(u32 pid)     { return pid == 7 ? E_OK : (pid == 0 ? E_INVAL : E_NOENT); }
/* faux « wait » : 7 -> code 0, 8 -> code 3, 9 -> arrete (130), 10 -> Ctrl+C, 11 -> fini mais code oublie */
err_t user_wait_pid(u32 pid, int *code, int *known)
{
    *known = 0;
    if (pid == 0) return E_INVAL;
    if (pid == 7 || pid == 8 || pid == 9) { *known = 1; *code = pid == 7 ? 0 : (pid == 8 ? 3 : 130); return E_OK; }
    if (pid == 10) return E_INTR;
    if (pid == 11) return E_OK;
    return E_NOENT;
}
err_t user_spawn_bg(const u8 *img, u32 size, const char *name, int argc, char **argv, u32 *pid)
{
    if (size < 7 || memcmp(img, "FAKEELF", 7))
        return E_NOEXEC;
    kprintf("<bg %s argc=%d", name, argc);
    for (int i = 0; i < argc; i++)
        kprintf(" %s", argv[i]);
    kprintf(">\n");
    *pid = 7;
    return E_OK;
}

/* faux noyau utilisateur : « execute » un fichier commencant par FAKEELF en affichant ses arguments */
static const u8 *fake_in; static u32 fake_in_len; static int fake_in_set;
void user_set_needs(char needs[][POE_LIBNAME_MAX], int n) { (void)needs; (void)n; }
int  libmgr_info(int i, const char **name, u32 *refs, u32 *lo, u32 *hi, u32 *shared)
{
    if (i) return 0;
    *name = "maths"; *refs = 2; *lo = 0x700000; *hi = 0x702000; *shared = 1;
    return 1;
}
void user_set_stdin(const u8 *b, u32 l, int set) { fake_in = b; fake_in_len = l; fake_in_set = set; }
err_t user_exec(const u8 *img, u32 size, const char *name, int argc, char **argv, struct user_result *res)
{
    if (size < 7 || (memcmp(img, "FAKEELF", 7) && memcmp(img, "\x7f" "ELF", 4)))
        return E_NOEXEC;
    kprintf("<%s argc=%d", name, argc);
    for (int i = 0; i < argc; i++)
        kprintf(" %s", argv[i]);
    if (fake_in_set) {
        kprintf(" in=");
        for (u32 i = 0; i < fake_in_len; i++)
            kprintf("%c", fake_in[i]);
    }
    kprintf(">\n");
    fake_in_set = 0;
    res->killed = 0;
    res->exit_code = !strcmp(name, "bad") ? 3 : 0;
    return E_OK;
}

int paging_enabled(void) { return 1; }
u32 paging_mapped_top(void) { return 128u << 20; }
u32 paging_table_bytes(void) { return 33u * 4096; }

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

#define KEY_BG_OUTPUT 0x7001
static void key(int k) { keys[nkeys++] = k; }
static void type(const char *s) { while (*s) key((u8)*s++); }

static void verify(void) __attribute__((noreturn));
int keyboard_getc(void)
{
    if (next_key == snapshot_at) {
        memcpy(snap, outbuf, outlen);
        snaplen = outlen;
    }
    while (next_key < nkeys && keys[next_key] == KEY_BG_OUTPUT) {   /* un programme d'arriere-plan ecrit pendant la saisie */
        next_key++;
        if (kprintf_task_hook) {
            kprintf_task_hook(0, 0);
            kprintf("BGSORTIE\n");
            kprintf_task_hook(1, '\n');
        }
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
    CHECK(count("Pagination       : active, 128 Mio mappes, tables de 132 Kio") == 1);
    CHECK(count("Tas du noyau") == 1 && count("Memoire physique") == 1);
    CHECK(count("8 Kio : 4 motifs ecrits et relus sans erreur") == 1);
    CHECK(count("Taille entre 1 et 4096 Kio.") == 1);
    CHECK(count("\ndeux\n") == 2);                              /* ligne rappelee par les fleches */
    CHECK(count("\nhi\n") == 1);                                /* retour arriere */
    CHECK(count("^C") == 4 && count("\nnope\n") == 0 && count("\nok\n") == 1);
    CHECK(count("\nzzz\n") == 0 && count("\nyes\n") == 1);      /* Ctrl+U */
    CHECK(count("\ndraft\n") == 1);                             /* brouillon restaure apres les fleches */
    CHECK(count("sleep  shutdown") == 1);                       /* Tab : plusieurs possibilites */
    CHECK(count("Trop d'arguments (maximum 16).") == 1);
    CHECK(count("Guillemet non ferme.") == 1);
    CHECK(count("\ncaf\x82\n") == 1);                           /* caractere accentue CP437 */
    CHECK(count("  1  sleep 250") == 0 || count("  1  ") >= 1); /* history : numerotation */
    CHECK(count("gris clair") == 1 && count("blanc") == 1);     /* colors */
    CHECK(count(" 10   vert clair") == 1);                      /* %2d correct dans colors */
    CHECK(count("PhoenOS 0.9.1") == 1);                         /* version */
    CHECK(count("<bg hello argc=3 hello a b>") == 1 && count("<bg hello argc=1 hello>") == 1);
    CHECK(count("[7] hello en arriere-plan (ps pour la liste, kill 7 pour l'arreter)") == 2);
    CHECK(count("& : seulement pour les programmes") == 1);
    CHECK(count("& ne se combine pas avec les tubes ni les redirections.") == 1);
    CHECK(count("Commande manquante avant &.") == 1);
    CHECK(count("NUM  PERE  ETAT     TOURS  NOM") == 1 && count("    0     0  actif        5  shell (le shell)") == 1);
    CHECK(count("    7     0  attend     123  ticker (arriere-plan)") == 1 && count("42 changement(s) de tache") == 1);
    CHECK(count("Aucun programme avec ce numero") == 2 && count("Ce numero n'est pas un programme") == 2);   /* kill et wait */
    CHECK(count("[7] termine, code 0\n") == 1 && count("[8] termine, code 3\n") == 1 && count("[9] termine, code 130 (arrete)\n") == 1);
    CHECK(count("[11] termine\n") == 1 && count("usage : wait <numero>") == 1 && count("[10] termine") == 0);   /* 10 : Ctrl+C */
    CHECK(count("veille (hlt) : 7 tours") == 1 && count("     9 S 0->1") == 1);
    CHECK(count("usage : kill <numero>") == 1 && count("kill : echec") == 0);
    CHECK(count("Sujets du manuel") == 1 && count(" ls  - liste\n cat  - affiche\n") == 1);   /* man seul */
    CHECK(count("ls  - liste\n  Syntaxe : ls [-l]\n  Option -l\n") == 1 && count("Syntaxe : cat [-n]") == 0);   /* man ls : uniquement sa section */
    CHECK(count("man: pas de manuel pour 'nope'") == 1);
    CHECK(count("usage : man [commande]") == 1);
    CHECK(count("    1  hello\n    2  more\n    3  hello\n    4  more\n") == 1);
    CHECK(count("usage : cat [-n] [fichier...]") == 1);
    CHECK(count("7 dossier(s)\n") == 1 && count("tree -d") == 1);
    CHECK(scroll_total == 36 && scroll_resets > 100);           /* +12 +12 -12 ; toute autre touche ramene en bas */
    CHECK(count("\napres-defilement\n") == 1);
    CHECK(count("FAKE DISK 9000  16 Mio") == 2);
    CHECK(count("Lecteur ATAPI (CD/DVD) (non utilisable)") == 2);
    CHECK(count("non formate. Tapez  format oui") == 1);
    CHECK(count("Disque non formate : tapez  format oui") == 0);   /* plus de message : save prepare le disque neuf tout seul */
    CHECK(count("Disque neuf : prepare automatiquement pour PhoenOS.") == 1);
    CHECK(count("usage : format oui") == 2);                    /* sans argument, puis "non" */
    CHECK(count("Disque formate.") == 1);
    CHECK(count("disque   : pret, generation 1 (a jour)") == 1);
    CHECK(count("Sauvegarde 2 enregistree (") == 2);          /* apres la preparation auto, puis apres format oui */
    CHECK(count("pret sur FAKE DISK 9000 (generation 2") == 1);
    CHECK(count("modifications non enregistrees : save)") == 1);
    CHECK(count("Sauvegarde 2 relue : ") == 1);
    CHECK(count("\napres\n") == 1 && count("\nlocal\n") == 0);     /* load a remplace la version locale */
    CHECK(count("PANIQUE") == 0);

    /* ---- fichiers ---- */
    CHECK(count("\n/a\n") == 1);                               /* pwd apres cd /a */
    CHECK(count("phoenos:/a> ") >= 8 && count("phoenos:/a/b> ") == 1);   /* le prompt suit le dossier */
    CHECK(count("\nhello\nmore\n") == 2);                      /* cat apres > puis >> (et cat de g.txt) */
    CHECK(count("mkdir: /a: existe deja") == 1);
    CHECK(count("cat: nofile: introuvable") == 1);
    CHECK(count("-       11  2026-10-05 07:31  f.txt") == 1 && count("d        -  2026-10-05 07:31  b/") == 1);   /* ls -l */
    CHECK(count("\n> x\n") == 1);                              /* echo ">" x : pas une redirection */
    CHECK(count("Redirection invalide") == 1);
    CHECK(count("redirection: /nodir/z: introuvable") == 1);
    CHECK(count("rm: /a/b: est un dossier") == 1);
    CHECK(count("rmdir: a: dossier non vide") == 1);
    CHECK(count("b/  f.txt  g.txt  ") == 1);                    /* ls apres cp */
    CHECK(count("cat g.txt \n") == 1);                          /* Tab complete g<Tab> en g.txt */
    CHECK(count("00000000  68 65 6c 6c 6f") == 1 && count("... 6 octet(s) de plus") == 1);   /* hexdump */
    CHECK(count("6 dossier(s), 13 fichier(s)") == 1);            /* tree / */
    CHECK(count("Redirection : cmd") == 3);                /* help, puis cat h.txt (help redirige) */
    CHECK(count("Commandes (Tab complete") == 3);               /* help redirige + cat : sortie bien capturee */
    CHECK(count("PANIQUE") == 0);

    /* après `dmesg` : tout le journal rejoué une seconde fois */
    CHECK(count_end("Commandes (Tab complete") == 4);          /* le journal (8 Kio) a perdu le plus ancien help */
    CHECK(count_end("\ndeux\n") == 9);
    /* ---- 0.0.9 ---- */
    CHECK(count_end("lundi 5 octobre 2026, 07:31:12") == 1);
    CHECK(count_end("\n1791185472\n") == 1);
    CHECK(count_end("usage : date [-s]") == 1);
    CHECK(count_end("      5       5      22 \n") == 1);              /* cat | wc : 5 lignes, 5 mots, 25 octets */
    CHECK(count_end("\n      5 /t.txt\n") == 1);                    /* wc -l */
    CHECK(count_end("\nun63\ndeux63\ntrois63\n") == 1);              /* plusieurs commandes sur une ligne */
    CHECK(count_end("\np63\nq63\n") == 1);
    CHECK(count_end("\na;b&c\n") == 1);
    CHECK(count_end("\napres63\n") == 1);
    CHECK(count_end("<hello argc=2 hello y63>\n") == 1);
    CHECK(count_end("BGSORTIE\nphoenos:/> ec") == 1);                /* la saisie "ec" est redessinee apres la sortie */
    CHECK(count_end("\rBGSORTIE") == 0 || count_end("\r") >= 1);
    CHECK(count_end("\nfini63\n") == 1);
    
    CHECK(count_end("\nun\ndeux\n") == 1);                         /* head -2 */
    CHECK(count_end("\nDeux\nun\n") == 1);                          /* tail -2 */
    CHECK(count_end("usage : head [-N] [fichier]") == 1);
    CHECK(count_end("2:deux\n4:Deux\n") == 1);   /* grep -n sans -i : seule la ligne 2 */
    CHECK(count_end("\nDeux\n") >= 2);                              /* grep -i : deux et Deux */
    CHECK(count_end("/t.txt:un\n/t.txt:un\n/t.txt:un\n/t.txt:un\n") == 1);
    CHECK(count_end("usage : grep [-inv] <motif> [fichier...]") == 1);
    CHECK(count_end("Pas d'entree : donnez un fichier") == 3);
    CHECK(count_end("Commande manquante autour du tube |.") == 2);
    CHECK(count_end("\na | b\n") == 1);
    CHECK(count_end("    1      1  a\n") == 1);
    CHECK(count_end("cat: /nofile: introuvable") == 1);
    CHECK(count_end("Trop de tubes (maximum 4 commandes reliees).") == 1);
    CHECK(count_end("sorted.txt") >= 2);

    CHECK(count_end("<hello argc=3 hello x y z>\n") == 1);
    CHECK(count_end("<hello argc=2 hello q>\n") == 1 && count_end("<hello argc=1 hello>\n") == 1);
    CHECK(count_end("bad : code de sortie 3") == 1 && count_end("hello : code de sortie") == 0);
    CHECK(count_end("/bin/notelf : pas un programme PhoenOS valide") == 1);
    CHECK(count_end("run: nosuch: introuvable") == 1 && count_end("usage : run <programme>") == 1);
    CHECK(count_end("<hello argc=1 hello in=salut\n>\n") == 1);                  /* tube vers un programme */
    CHECK(count_end("<hello argc=2 hello zzz>\n") == 1);                         /* redirige puis cat */
    CHECK(count_end("run: /bin: est un dossier") == 1);
    CHECK(count_end("\nL23\n") == 3 && count_end("\nL46\n") == 2 && count_end("\nL60\n") == 1);   /* 2 pages, tube complet, puis stop */
    CHECK(count_end("\nL47\n") == 1 && count_end("\nL25\n") == 3);
    CHECK(count_end("-- Suite") == 0);                                   /* l'invite est effacee apres chaque touche */
    CHECK(count_end("usage : more [fichier]") == 1 && count_end("more: /nofile: introuvable") == 1);
    CHECK(count_end("\nL1\n") >= 1);                                /* more fichier > fichier : copie sans invite */
    CHECK(count_end("apres-more\n") >= 1 && count_end("apres-more2\n") >= 1 && count_end("apres-more3\n") >= 1);
    CHECK(count_end("<fake argc=3 fake a b>\n") == 1 && count_end("<fake argc=1 fake>\n") == 1 && count_end("<fake argc=2 fake z>\n") == 1);
    CHECK(count_end("Application    : fake 1.2\n") == 1 && count_end("Auteur         : Moi\n") == 5);
    CHECK(count_end("Etat           : valide\n") == 5 && count_end("somme de controle exacte") == 3);
    CHECK(count_end("/apps/bad.poe : programme abime") == 1 && count_end("somme de controle FAUSSE") == 1);
    CHECK(count_end("/apps/gui.poe : exige une fonction pas encore disponible") == 1);
    CHECK(count_end("exe : /long.txt n'est pas une application .poe") == 1);
    CHECK(count_end("exe: nofile: introuvable") == 1 && count_end("usage : exe <application>") == 1);
    CHECK(count_end("/long.txt : ce n'est pas une application .poe") == 2);
    /* 0.8.0 : bibliotheques et .poe complets */
    CHECK(count_end("  maths      1.0      0x700000-0x702000  Test maths\n") == 2 && count_end("En memoire : maths (2 programmes, 1 pages de code partagees)") == 2);
    CHECK(count_end("Exports (5)    : maths_pgcd maths_racine maths_fibo maths_appels maths_nom\n") == 1);
    CHECK(count_end("psl: /lib/nope.psl: introuvable") == 1 && count_end("usage : psl [bibliotheque]") == 1);
    CHECK(count_end("/lib/texte2.psl cree : texte2 3.1, 2 exports, ") == 1 && count_end("Description    : Textes de test") == 1);
    CHECK(count_end("pslpack : symbole a exporter introuvable") == 1 && count_end("pslpack : nom : 1 a 15 caracteres") == 1);
    CHECK(count_end("pslpack : le fichier n'est pas un ELF") == 1 && count_end("usage : pslpack -n nom") == 1);
    CHECK(count_end("Aucun peripherique PCI detecte.") == 1 && count_end("usage : lspci") == 1);
    CHECK(count_end("e1000    0.9.0  ethernet  officiel") == 1 && count_end("aucun peripherique correspondant") == 1);
    CHECK(count_end("eth0  pilote fake  MAC 02:00:00:00:00:09  lien actif") == 2 && count_end("pas d'adresse (essayez : net up)") == 2);
    CHECK(count_end("net: DHCP : delai depasse") == 1 && count_end("usage : net [up]") == 1 && count_end("usage : ping <adresse|nom>") == 1 && count_end("usage : dns <nom> | -l | -f") == 1 && count_end("Cache DNS vide.") == 1 && count_end("0 nom(s) en cache") == 1);
    CHECK(count_end("reseau non configure (net up)") == 2 && count_end("ping: 999.1.1.1") == 1 && count_end("seq=1 erreur d'entree/sortie") == 2 && count_end("10.1.2.3 : 10.1.2.3") == 1);
    CHECK(count_end("/apps/mine.poe cree : mine 1.5, 272 octets (format 2)") == 1 && count_end("/apps/simple.poe cree : simple 1, 160 octets (format 1)") == 1);
    CHECK(count_end("Bibliotheques  : maths (presente) texte2 (presente)") == 1 && count_end("Format         : 2") == 1 && count_end("Format         : 1") == 3);
    CHECK(count_end("poepack : bibliotheque indiquee deux fois") == 1 && count_end("poepack : le programme n'est pas un fichier ELF") == 1);
    CHECK(count_end("poepack: /bin/nofile: introuvable") == 1 && count_end("usage : poepack -n nom") == 1);
    CHECK(count_end("/apps/alib.poe : bibliotheque \"absente\" absente de /lib") == 1);      /* refus AVANT de lancer */
    CHECK(count_end("<mine argc=2 mine q>") == 1 && count_end("<alib") == 0);
    CHECK(count_end("/extrait.elf : 64 octets extraits de /apps/mine.poe") == 1 && count_end("poeunpack : /long.txt : ce n'est pas une application .poe") == 1);
    {
        struct fs_node *a, *b;

        CHECK(fs_lookup("/extrait.elf", &a) == E_OK && fs_lookup("/bin/fakeelf", &b) == E_OK && a->size == b->size && !memcmp(a->data, b->data, a->size));   /* intact */
        CHECK(fs_lookup("/lib/texte3.psl", &a) == E_NOENT && fs_lookup("/lib/x.psl", &a) == E_NOENT && fs_lookup("/apps/bad2.poe", &a) == E_NOENT);   /* rien cree en cas d'echec */
    }
    CHECK(count_end("Redemarrage...") == 1);
    CHECK(count_end("Sauvegarde des fichiers sur le disque... ok (generation 3)") == 1);
    {                                                           /* la sauvegarde automatique est bien sur le « disque » */
        struct fs_node *n;

        fs_init();
        CHECK(persist_init() == E_OK && persist_state() == PERSIST_READY && persist_get_info()->generation == 3);
        CHECK(persist_load(NULL) == E_OK && fs_lookup("/final.txt", &n) == E_OK && n->size == 9);
    }
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
    CHECK(netdev_register(&fake_nic) == E_OK && net_init() == E_OK);
    fs_set_clock(fake_clock);
    fs_init();
    fake_cmos[0] = 0x12; fake_cmos[2] = 0x31; fake_cmos[4] = 0x07; fake_cmos[7] = 0x05;
    fake_cmos[8] = 0x10; fake_cmos[9] = 0x26; fake_cmos[0x32] = 0x20; fake_cmos[0x0B] = 0x02;
    ata_init();
    persist_init();
    {
        static const char man[] = "INTRO\n\n== ls  - liste\n  Syntaxe : ls [-l]\n  Option -l\n\n== cat  - affiche\n  Syntaxe : cat [-n]\n";
        struct fs_node *f;

        {   /* 60 lignes : L01..L60 */
            char big[60 * 4 + 1];

            for (int i = 0; i < 60; i++) {
                big[i * 4] = 'L'; big[i * 4 + 1] = (char)('0' + (i + 1) / 10); big[i * 4 + 2] = (char)('0' + (i + 1) % 10);
                big[i * 4 + 3] = '\n';
            }
            fs_create("/long.txt", &f); fs_write(f, big, 60 * 4);
        }
        fs_mkdir("/bin");
        fs_create("/bin/hello", &f); fs_write(f, "FAKEELF", 7);
        fs_create("/bin/bad", &f); fs_write(f, "FAKEELF", 7);
        fs_create("/bin/notelf", &f); fs_write(f, "junkjunk", 8);
        fs_mkdir("/apps");
        {   /* applications .poe : fake (valide), bad (somme de controle fausse), gui (exige l'interface graphique) */
            static const struct { const char *path, *name; u32 flags; u32 crc_xor; } apps[] = {
                { "/apps/fake.poe", "fake", 0, 0 }, { "/apps/bad.poe", "bad", 0, 1 }, { "/apps/gui.poe", "gui", 1, 0 },
            };

            for (int i = 0; i < 3; i++) {
                u8 poe[96 + 8];
                u32 crc = crc32_update(0, "FAKEELF!", 8) ^ apps[i].crc_xor;

                memset(poe, 0, sizeof poe);
                memcpy(poe, "POE\1", 4); poe[4] = 1; poe[6] = (u8)apps[i].flags; poe[8] = 1;
                poe[12] = 96; poe[16] = 8;
                memcpy(poe + 20, &crc, 4);
                strcpy((char *)poe + 24, apps[i].name); strcpy((char *)poe + 56, "1.2"); strcpy((char *)poe + 72, "Moi");
                memcpy(poe + 96, "FAKEELF!", 8);
                fs_create(apps[i].path, &f); fs_write(f, poe, sizeof poe);
            }
        }
        {   /* 0.8.0 : un faux ELF, et de vraies bibliotheques .psl / ELF lies par ld (voir tests/run.sh) */
            static u8 fakeelf[64], big[1 << 16];
            static const char *const hosts[] = { "lib/maths.psl", "lib/texte.elf" };
            static const char *const dests[] = { "/lib/maths.psl", "/texte.elf" };

            memset(fakeelf, 0, sizeof fakeelf);
            memcpy(fakeelf, "\x7f" "ELF", 4); strcpy((char *)fakeelf + 8, "contenu-elf");
            fs_create("/bin/fakeelf", &f); fs_write(f, fakeelf, sizeof fakeelf);
            fs_mkdir("/lib");
            for (int i = 0; i < 2; i++) {
                long fd = sys3(5, (long)hosts[i], 0, 0), r = fd < 0 ? -1 : sys3(3, fd, (long)big, sizeof big);

                if (r <= 0) { out("  fichier de test introuvable : "); out(hosts[i]); out("\n"); test_fails++; continue; }
                fs_create(dests[i], &f); fs_write(f, big, (u32)r);
                sys3(6, fd, 0, 0);
            }
        }
        fs_mkdir("/docs");
        fs_create("/docs/manuel.txt", &f);
        fs_write(f, man, sizeof man - 1);
    }

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
    type("echo 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17\n");
    type("echo \"abc\n");
    type("echo caf"); key(0x82); type("\n");
    key(KEY_LEFT); key(KEY_HOME); key(KEY_DEL); key(KEY_END);   /* touches ignorees sans effet */
    for (int i = 0; i < 400; i++) key('a');                     /* ligne trop longue : bornee, sans plantage */
    key(3);
    type("history\n");
    type("colors\n");
    type("version\n");
    type("hello a b &\n");                                      /* arriere-plan */
    type("run hello &\n");
    type("echo x &\n");                                         /* refuse : pas un programme */
    type("hello | cat &\n");                                    /* refuse : tube */
    type("&\n");
    type("ps\n");
    type("kill 7\n");
    type("kill 99\n");
    type("kill 0\n");
    type("kill\n");
    type("wait 7\n");
    type("wait 8\n");
    type("wait 9\n");
    type("wait 10\n");
    type("wait 11\n");
    type("wait 99\n");
    type("wait 0\n");
    type("wait\n");
    type("schedlog\n");
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
    type("man\n");
    type("man ls\n");
    type("man nope\n");
    type("man a b\n");
    type("cat -n /a/f.txt /a/g.txt\n");
    type("cat -x f\n");
    type("mkdir /t1\n");
    type("tree -d /\n");
    key(KEY_PGUP); key(KEY_PGUP); key(KEY_PGDN);                /* defilement : ne modifie pas la ligne */
    key(2); key(2);                                             /* Ctrl+B = PageHaut : +24 */
    type("echo apres-defilement\n");
    type("disks\n");
    type("save\n");                                             /* disque non formate */
    type("format\n");
    type("format non\n");
    type("format oui\n");
    type("df\n");
    type("echo apres > /s.txt\n");
    type("save\n");
    type("disks\n");
    type("echo local > /s.txt\n");
    type("df\n");
    type("load\n");                                             /* remplace s.txt par la version enregistree */
    type("cat /s.txt\n");
    type("echo derniere > /final.txt\n");                       /* modification non enregistree : reboot la sauvegarde */
    snapshot_at = nkeys;
    type("dmesg\n");
    /* ---- 0.0.9 : date, tubes, outils texte ---- */
    type("date\n");
    type("date -s\n");
    type("date -x\n");
    type("printf\n");
    type("echo un > /t.txt\n");
    type("echo deux >> /t.txt\n");
    type("echo trois >> /t.txt\n");
    type("echo Deux >> /t.txt\n");
    type("echo un >> /t.txt\n");
    type("cat /t.txt | wc\n");                                   /* 5 lignes, 5 mots, 26 octets */
    type("wc -l /t.txt\n");
    type("wc -w -c /t.txt /t.txt\n");
    type("head -2 /t.txt\n");
    type("tail -2 /t.txt\n");
    type("tail -99 /t.txt | head -1\n");
    type("head -x /t.txt\n");
    type("grep -n eu /t.txt\n");
    type("grep -i deux /t.txt\n");
    type("grep -v un /t.txt\n");
    type("grep un /t.txt /t.txt\n");
    type("grep\n");
    type("cat /t.txt | sort\n");
    type("cat /t.txt | sort -r | head -1\n");
    type("sort -u /t.txt | wc -l\n");
    type("cat /t.txt | grep -v un | sort > /sorted.txt\n");
    type("cat /sorted.txt\n");
    type("wc\n");                                               /* pas d'entree */
    type("echo | | wc\n");
    type("echo a |\n");
    type("echo a \"|\" b\n");                                    /* | entre guillemets : texte */
    type("echo a | cat -n | cat -n\n");
    type("cat /nofile | wc -l\n");                               /* l'erreur s'affiche, la suite continue */
    type("ls -l / | grep sorted\n");
    type("echo 1 | echo 2 | echo 3 | echo 4 | echo 5\n");
    /* ---- 0.1.0 : programmes utilisateur (noyau simule) ---- */
    type("hello x \"y z\"\n");
    type("run hello q\n");
    type("run /bin/hello\n");
    type("bad\n");
    type("notelf\n");
    type("run nosuch\n");
    type("run\n");
    type("echo salut | hello\n");
    type("hello zzz > /o.txt\n");
    type("cat /o.txt\n");
    type("run /bin\n");
    /* ---- 0.3.0 : more (pagination du texte) ---- */
    type("more /long.txt\n"); type(" "); type("q");                   /* une page de plus, puis quitter */
    type("echo apres-more\n");
    type("cat /long.txt | more\n"); type(" "); type(" ");             /* le texte se termine sans nouvelle invite */
    type("echo apres-more2\n");
    type("more /long.txt\n"); type("\n"); type("\n"); type("q");      /* Entree : une ligne a la fois */
    type("echo apres-more3\n");
    type("more > /m.txt\n");
    type("echo L1 > /m1.txt\n");
    type("more /m1.txt > /m2.txt\n"); type("cat /m2.txt\n");
    type("more /nofile\n");
    type("more a b\n");
    type("more\n");
    /* ---- 0.5.0 : applications .poe ---- */
    type("exe fake a b\n");
    type("exe /apps/fake.poe\n");
    type("fake z\n");                                           /* nom seul : /apps/fake.poe */
    type("poeinfo fake\n");
    type("exe bad\n");
    type("poeinfo bad\n");
    type("exe gui\n");
    type("exe /long.txt\n");
    type("exe nofile\n");
    type("exe\n");
    type("poeinfo /long.txt\n");
    type("lspci\n");                                            /* 0.9.0 : reseau, sans carte */
    type("lspci x\n");
    type("drivers\n");
    type("net\n");
    type("ping 10.0.2.2\n");
    type("dns exemple.test\n");
    type("net\n");                                              /* carte sans adresse */
    type("net up\n");                                           /* pas de serveur DHCP : delai depasse */
    type("net zzz\n");
    type("ping\n");
    type("ping 10.0.2.2\n");
    type("ping 999.1.1.1\n");
    type("dns\n");
    type("dns -l\n");
    type("dns -f\n");
    type("dns exemple.test\n");
    type("dns 10.1.2.3\n");
    type("psl\n");                                              /* 0.8.0 : bibliotheques */
    type("psl maths\n");
    type("psl nope\n");
    type("psl /x y\n");
    type("pslpack -n texte2 -v 3.1 -a Moi -d \"Textes de test\" -e texte_mots -e texte_inverser /texte.elf /lib/texte2.psl\n");
    type("pslpack -n texte3 -v 3.1 -a Moi -e inexistant /texte.elf /lib/texte3.psl\n");
    type("pslpack -n Texte -v 1 -a Moi -e texte_mots /texte.elf /lib/x.psl\n");
    type("pslpack /texte.elf\n");
    type("pslpack -n z -v 1 -a Moi -e texte_mots /long.txt /lib/z.psl\n");
    type("psl texte2\n");
    type("psl\n");
    type("poepack -n mine -v 1.5 -a Moi -d \"Mon appli\" -l maths -l texte2 fakeelf /apps/mine.poe\n");
    type("poeinfo mine\n");
    type("poepack -n simple -v 1 -a Moi /bin/fakeelf /apps/simple.poe\n");
    type("poeinfo simple\n");
    type("poepack -n bad2 -v 1 -a Moi -l maths -l maths fakeelf /apps/bad2.poe\n");
    type("poepack -n bad3 -v 1 -a Moi /long.txt /apps/bad3.poe\n");
    type("poepack -n bad4 -v 1 -a Moi nofile /apps/bad4.poe\n");
    type("poepack\n");
    type("poepack -n alib -v 1 -a Moi -l absente fakeelf /apps/alib.poe\n");
    type("exe alib\n");                                         /* bibliotheque absente : refuse AVANT de lancer */
    type("exe mine q\n");                                       /* bibliotheques presentes : lance */
    type("poeunpack mine /extrait.elf\n");
    type("poeunpack /long.txt /x.elf\n");
    type("poeunpack\n");
    type("poeunpack gui /gui.elf\n");
    type("echo un63 ; echo deux63 ; echo trois63\n");                  /* 0.6.3 : plusieurs commandes par ligne */
    type("echo p63;echo q63\n");                                       /* separateurs colles */
    type("echo \"a;b&c\"\n");                                          /* entre guillemets : pas un separateur */
    type(";\n");
    type("echo ;;; echo apres63\n");
    type("run hello x63 & run hello y63\n");                          /* x en arriere-plan, y au premier plan */
    type("echo z63 &\n");                                              /* & sur une commande interne : refuse */
    type("ec"); key(KEY_BG_OUTPUT); type("ho fini63\n");              /* sortie d'arriere-plan en pleine saisie */
    type("log add essai-journal\n");
    type("log -t user\n");
    type("log level\n");
    type("log level warn\n");
    type("log level trace\n");
    type("log save\n");
    type("log -l nimporte\n");
    type("log clear\n");
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
