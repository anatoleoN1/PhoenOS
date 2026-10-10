/*
 * PhoenOS - shell.c
 * Shell en ligne de commande : édition de ligne, historique (flèches haut/bas),
 * complétion par Tab (commandes et chemins), redirection > et >>, commandes intégrées.
 *
 * Ajouter une commande : écrire `static err_t cmd_xxx(int argc, char **argv)`
 * puis l'inscrire dans la table `commands` plus bas.
 */
#include "rtc.h"
#include "user.h"
#include "sched.h"
#include "poe.h"
#include "paging.h"
#include "shell.h"
#include "cmdline.h"
#include "version.h"
#include "types.h"
#include "error.h"
#include "kprintf.h"
#include "vga.h"
#include "pit.h"
#include "pmm.h"
#include "kheap.h"
#include "keyboard.h"
#include "power.h"
#include "panic.h"
#include "string.h"
#include "fs.h"
#include "ata.h"
#include "persist.h"
#include "io.h"

#define LINE_MAX 256
#define MAX_ARGS 16
#define MAX_STAGES 4                     /* commandes reliees par des tubes */
#define SCROLL_STEP 12                   /* lignes par appui sur PageHaut / PageBas */
#define CAPTURE_MAX (128u * 1024)       /* sortie maximale d'une commande redirigée */

typedef err_t (*cmd_fn)(int argc, char **argv);

struct command {
    const char *group;
    const char *name;
    const char *usage;      /* arguments, pour l'aide */
    const char *help;
    cmd_fn      fn;
};

static struct history hist;

/* Vrai pendant une redirection : cat copie alors les octets tels quels. */
static int raw_output;
static int bg_mode;                              /* 1 : la ligne se termine par & (programme en arriere-plan) */

/* Entree standard : la sortie de la commande precedente d'un tube (sinon absente). */
static const u8 *stdin_buf;
static u32 stdin_len;
static int has_stdin;

/* Prototypes des commandes (la table qui les référence est juste en dessous). */
static err_t cmd_help(int argc, char **argv);
static err_t cmd_clear(int argc, char **argv);
static err_t cmd_version(int argc, char **argv);
static err_t cmd_uptime(int argc, char **argv);
static err_t cmd_mem(int argc, char **argv);
static err_t cmd_memtest(int argc, char **argv);
static err_t cmd_echo(int argc, char **argv);
static err_t cmd_sleep(int argc, char **argv);
static err_t cmd_ps(int argc, char **argv);
static err_t cmd_kill(int argc, char **argv);
static err_t cmd_schedlog(int argc, char **argv);
static err_t cmd_colors(int argc, char **argv);
static err_t cmd_dmesg(int argc, char **argv);
static err_t cmd_history(int argc, char **argv);
static err_t cmd_crash(int argc, char **argv);
static err_t cmd_reboot(int argc, char **argv);
static err_t cmd_shutdown(int argc, char **argv);
static err_t cmd_halt(int argc, char **argv);
static err_t cmd_ls(int argc, char **argv);
static err_t cmd_cat(int argc, char **argv);
static err_t cmd_cd(int argc, char **argv);
static err_t cmd_pwd(int argc, char **argv);
static err_t cmd_mkdir(int argc, char **argv);
static err_t cmd_touch(int argc, char **argv);
static err_t cmd_rm(int argc, char **argv);
static err_t cmd_rmdir(int argc, char **argv);
static err_t cmd_cp(int argc, char **argv);
static err_t cmd_mv(int argc, char **argv);
static err_t cmd_tree(int argc, char **argv);
static err_t cmd_df(int argc, char **argv);
static err_t cmd_hexdump(int argc, char **argv);
static err_t cmd_man(int argc, char **argv);
static err_t cmd_disks(int argc, char **argv);
static err_t cmd_format(int argc, char **argv);
static err_t cmd_save(int argc, char **argv);
static err_t cmd_load(int argc, char **argv);
static err_t cmd_date(int argc, char **argv);
static err_t cmd_run(int argc, char **argv);
static err_t cmd_exe(int argc, char **argv);
static err_t cmd_poeinfo(int argc, char **argv);
static err_t cmd_head(int argc, char **argv);
static err_t cmd_tail(int argc, char **argv);
static err_t cmd_wc(int argc, char **argv);
static err_t cmd_grep(int argc, char **argv);
static err_t cmd_sort(int argc, char **argv);
static err_t cmd_more(int argc, char **argv);
static void erase_line(usize len);

/* ------------------------------------------------------------------ */
/* Table des commandes (ordre = ordre d'affichage de `help`)           */
/* ------------------------------------------------------------------ */

#define G_SYS "Systeme"
#define G_FS  "Fichiers"
#define G_DISK "Stockage"
#define G_TXT "Texte"

static const struct command commands[] = {
    { G_SYS, "help",     "[commande]",   "Liste les commandes, ou detaille l'une d'elles",  cmd_help     },
    { G_SYS, "man",      "[commande]",   "Manuel : detail, options et exemples",           cmd_man      },
    { G_SYS, "clear",    "",             "Efface l'ecran (Ctrl+L aussi)",                  cmd_clear    },
    { G_SYS, "version",  "",             "Version et date de compilation",                 cmd_version  },
    { G_SYS, "uptime",   "",             "Temps depuis le demarrage",                      cmd_uptime   },
    { G_SYS, "mem",      "",             "Etat de la memoire physique et du tas",          cmd_mem      },
    { G_SYS, "memtest",  "[Kio]",        "Teste le tas : ecrit et relit des motifs",       cmd_memtest  },
    { G_SYS, "date",     "[-s]",         "Date et heure (-s : secondes depuis 1970)",      cmd_date     },
    { G_SYS, "exe",      "<application> [args]", "Lance une application .poe (cherche aussi dans /apps)", cmd_exe },
    { G_SYS, "poeinfo",  "<application>",     "Montre l'en-tete d'une application .poe", cmd_poeinfo },
    { G_SYS, "run",      "<programme> [args]", "Lance un programme (ex. run /bin/hello ; /bin/ implicite)", cmd_run },
    { G_SYS, "ps",       "",             "Liste les programmes en cours (taches)",         cmd_ps       },
    { G_SYS, "schedlog", "",             "Journal de l'ordonnanceur (diagnostic)",        cmd_schedlog },
    { G_SYS, "kill",     "<numero>",     "Arrete un programme (voir ps)",                  cmd_kill     },
    { G_SYS, "sleep",    "<ms>",         "Attend un nombre de millisecondes",              cmd_sleep    },
    { G_SYS, "colors",   "",             "Affiche les 16 couleurs du mode texte",          cmd_colors   },
    { G_SYS, "dmesg",    "",             "Rejoue tout le journal du noyau",                cmd_dmesg    },
    { G_SYS, "history",  "",             "Liste les commandes precedentes",                cmd_history  },
    { G_SYS, "crash",    "<type>",       "Provoque une erreur : voir l'ecran d'erreur",    cmd_crash    },
    { G_SYS, "reboot",   "",             "Redemarre la machine",                           cmd_reboot   },
    { G_SYS, "shutdown", "",             "Eteint la machine (QEMU, VirtualBox)",           cmd_shutdown },
    { G_SYS, "halt",     "",             "Arrete le processeur",                           cmd_halt     },
    { G_FS,  "ls",       "[-l] [chemin]", "Liste un dossier (-l : type, taille, date)",        cmd_ls       },
    { G_FS,  "cat",      "[-n] [fichier...]", "Affiche des fichiers ou l'entree (-n : numerote)",                 cmd_cat      },
    { G_FS,  "cd",       "[dossier]",    "Change de dossier (sans argument : /)",          cmd_cd       },
    { G_FS,  "pwd",      "",             "Affiche le dossier courant",                     cmd_pwd      },
    { G_FS,  "mkdir",    "[-p] <dossier>...", "Cree des dossiers (-p : avec les parents)", cmd_mkdir    },
    { G_FS,  "touch",    "<fichier>...", "Cree des fichiers vides",                        cmd_touch    },
    { G_FS,  "rm",       "[-r] <chemin>...", "Supprime (-r : dossiers et contenu)",        cmd_rm       },
    { G_FS,  "rmdir",    "<dossier>...", "Supprime des dossiers vides",                    cmd_rmdir    },
    { G_FS,  "cp",       "<source> <destination>", "Copie un fichier",                     cmd_cp       },
    { G_FS,  "mv",       "<source> <destination>", "Deplace ou renomme",                   cmd_mv       },
    { G_FS,  "tree",     "[-d] [dossier]", "Affiche l'arborescence (-d : dossiers seuls)",                         cmd_tree     },
    { G_FS,  "df",       "",             "Occupation du systeme de fichiers",              cmd_df       },
    { G_FS,  "hexdump",  "<fichier> [octets]", "Affiche un fichier en hexadecimal",        cmd_hexdump  },
    { G_DISK, "disks",   "",             "Liste les disques et l'etat du stockage",        cmd_disks    },
    { G_DISK, "format",  "oui",          "Prepare le disque pour PhoenOS (efface la sauvegarde)", cmd_format },
    { G_DISK, "save",    "",             "Enregistre les fichiers sur le disque",          cmd_save     },
    { G_DISK, "load",    "",             "Relit la sauvegarde du disque",                  cmd_load     },
    { G_TXT, "echo",     "[-n] [texte...]", "Affiche son texte (\"entre guillemets\" permis)", cmd_echo },
    { G_TXT, "head",     "[-N] [fichier]", "Affiche les N premieres lignes (10 par defaut)", cmd_head },
    { G_TXT, "tail",     "[-N] [fichier]", "Affiche les N dernieres lignes (10 par defaut)", cmd_tail },
    { G_TXT, "wc",       "[-lwc] [fichier...]", "Compte lignes, mots et octets",           cmd_wc       },
    { G_TXT, "grep",     "[-inv] <motif> [fichier...]", "Garde les lignes contenant le motif (-i casse, -n numeros, -v inverse)", cmd_grep },
    { G_TXT, "more",     "[fichier]",    "Lit un texte page par page (Espace, Entree, q)", cmd_more },
    { G_TXT, "sort",     "[-ru] [fichier]", "Trie les lignes (-r : inverse, -u : sans doublons)", cmd_sort },
};

#define COMMAND_COUNT ((int)(sizeof(commands) / sizeof(commands[0])))


/* ------------------------------------------------------------------ */
/* Utilitaires d'affichage                                             */
/* ------------------------------------------------------------------ */

static void color(enum vga_color fg)
{
    vga_set_color(fg, VGA_BLACK);
}

static err_t usage(const char *text)
{
    color(VGA_YELLOW);
    keprintf("usage : %s\n", text);
    color(VGA_LIGHT_GREY);
    return E_REPORTED;                          /* déjà expliqué : execute() n'ajoute rien */
}

/* Erreur de la forme  commande: chemin: cause  (jamais redirigée). */
static err_t fail_path(const char *cmd, const char *path, err_t e)
{
    color(VGA_LIGHT_RED);
    keprintf("%s: %s: %s\n", cmd, path, err_str(e));
    color(VGA_LIGHT_GREY);
    return E_REPORTED;
}

static err_t fail_msg(const char *msg)
{
    color(VGA_LIGHT_RED);
    keprintf("%s\n", msg);
    color(VGA_LIGHT_GREY);
    return E_REPORTED;
}

/* Options (-r, -p, -l) en tête des arguments : le bit i de *flags correspond à allowed[i]. */
static err_t parse_flags(int argc, char **argv, const char *allowed, u32 *flags, int *first)
{
    int i = 1;

    *flags = 0;
    for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
        for (const char *c = argv[i] + 1; *c; c++) {
            const char *p = allowed;
            int bit = 0;

            while (*p && *p != *c) {
                p++;
                bit++;
            }
            if (!*p)
                return E_INVAL;
            *flags |= 1u << bit;
        }
    }
    *first = i;
    return E_OK;
}

/* ------------------------------------------------------------------ */
/* Commandes                                                           */
/* ------------------------------------------------------------------ */

static err_t cmd_help(int argc, char **argv)
{
    static const char *const groups[] = { G_SYS, G_FS, G_DISK, G_TXT };

    if (argc >= 2) {
        for (int i = 0; i < COMMAND_COUNT; i++) {
            if (strcmp(commands[i].name, argv[1]) == 0) {
                kprintf("%s %s\n  %s\n", commands[i].name, commands[i].usage, commands[i].help);
                return E_OK;
            }
        }
        return fail_path("help", argv[1], E_NOENT);
    }

    kprintf("Commandes (Tab complete, fleches haut/bas = historique) :\n");
    for (usize g = 0; g < sizeof(groups) / sizeof(groups[0]); g++) {
        usize col;

        color(VGA_YELLOW);
        kprintf(" %-9s", groups[g]);
        col = 10;
        color(VGA_LIGHT_CYAN);
        for (int i = 0; i < COMMAND_COUNT; i++) {
            usize w = strlen(commands[i].name) + 1;

            if (strcmp(commands[i].group, groups[g]) != 0)
                continue;
            if (col + w > 79) {
                kprintf("\n          ");
                col = 10;
            }
            kprintf("%s ", commands[i].name);
            col += w;
        }
        color(VGA_LIGHT_GREY);
        kprintf("\n");
    }
    kprintf("Redirection : cmd > fichier  ou  cmd >> fichier (ajoute).  Tube : cmd | cmd  (espaces obligatoires)\n");
    kprintf("Tapez man <commande> pour le manuel, help <commande> pour un rappel.\n");
    return E_OK;
}

static err_t cmd_clear(int argc, char **argv)
{
    (void)argc; (void)argv;
    vga_clear();
    return E_OK;
}

static err_t cmd_version(int argc, char **argv)
{
    (void)argc; (void)argv;
    kprintf("%s %s - noyau x86 32 bits, compile le %s a %s\n",
            PHOENOS_NAME, PHOENOS_VERSION, __DATE__, __TIME__);
    return E_OK;
}

static err_t cmd_uptime(int argc, char **argv)
{
    u32 s = pit_uptime_ms() / 1000;

    (void)argc; (void)argv;
    kprintf("Allume depuis %u h %u min %u s (%u ticks)\n",
            s / 3600, (s / 60) % 60, s % 60, pit_ticks());
    return E_OK;
}

static err_t cmd_mem(int argc, char **argv)
{
    struct kheap_stats st;

    (void)argc; (void)argv;
    kheap_get_stats(&st);

    kprintf("Memoire physique : %u Kio detectes, %u Kio libres\n",
            pmm_usable_frames() * 4, pmm_free_frames() * 4);
    if (paging_enabled())
        kprintf("Pagination       : active, %u Mio mappes, tables de %u Kio\n",
                paging_mapped_top() >> 20, paging_table_bytes() / 1024);
    else
        kprintf("Pagination       : inactive\n");
    kprintf("Tas du noyau     : %u Kio au total\n", kheap_size() / 1024);
    kprintf("  utilise        : %u octets dans %u bloc(s)\n", st.used_bytes, st.used_blocks);
    kprintf("  libre          : %u octets dans %u bloc(s)\n", st.free_bytes, st.free_blocks);
    return E_OK;
}

static err_t cmd_echo(int argc, char **argv)
{
    int first = 1, newline = 1;

    if (argc >= 2 && strcmp(argv[1], "-n") == 0) {
        newline = 0;
        first = 2;
    }
    for (int i = first; i < argc; i++)
        kprintf("%s%s", i > first ? " " : "", argv[i]);
    if (newline)
        kprintf("\n");
    return E_OK;
}

static err_t cmd_sleep(int argc, char **argv)
{
    u32 ms;

    if (argc != 2 || cmdline_parse_u32(argv[1], &ms) != E_OK)
        return usage("sleep <millisecondes>");
    if (ms > 60000)
        return fail_msg("Maximum : 60000 ms.");
    if (sched_ready())
        sched_sleep_ms(ms);                     /* les programmes en arriere-plan continuent */
    else
        pit_sleep_ms(ms);
    return E_OK;
}

static err_t cmd_ps(int argc, char **argv)
{
    static const char *const state[] = { "libre", "pret", "actif", "attend", "fini" };
    struct task_info ti;

    (void)argv;
    if (argc != 1)
        return usage("ps");
    kprintf("  NUM  ETAT     TOURS  NOM\n");
    for (int i = 0; sched_info(i, &ti); i++)
        kprintf("%5u  %-7s %6u  %s%s\n", ti.id, state[ti.state], ti.ticks, ti.name,
                ti.id == 0 ? " (le shell)" : (ti.detached ? " (arriere-plan)" : ""));
    kprintf("%u changement(s) de tache depuis le demarrage\n", sched_switches());
    return E_OK;
}

static err_t cmd_schedlog(int argc, char **argv)
{
    u32 t, a, b;
    char k;

    (void)argv;
    if (argc != 1)
        return usage("schedlog");
    kprintf("veille (hlt) : %u tours ; evenements (tick, S a->b = commutation, B/W = bloque/reveille, X = fin) :\n", sched_idle_count());
    for (int i = 0; sched_trace_get(i, &t, &k, &a, &b); i++) {
        if (k == 'S')
            kprintf("%6u S %u->%u\n", t, a, b);
        else if (k == 'X')
            kprintf("%6u X %u code %d\n", t, a, (int)b);
        else
            kprintf("%6u %c %u\n", t, k, a);
    }
    return E_OK;
}

static err_t cmd_kill(int argc, char **argv)
{
    u32 pid;
    err_t e;

    if (argc != 2 || cmdline_parse_u32(argv[1], &pid) != E_OK)
        return usage("kill <numero>   (numeros : commande ps)");
    e = user_kill(pid);
    if (e == E_NOENT)
        return fail_msg("Aucun programme avec ce numero (voir ps).");
    if (e == E_INVAL)
        return fail_msg("Ce numero n'est pas un programme (le shell ne s'arrete pas).");
    return e;
}

static err_t cmd_colors(int argc, char **argv)
{
    static const char *const names[16] = {
        "noir", "bleu", "vert", "cyan", "rouge", "magenta", "marron", "gris clair",
        "gris fonce", "bleu clair", "vert clair", "cyan clair", "rouge clair",
        "magenta clair", "jaune", "blanc"
    };

    (void)argc; (void)argv;
    for (int c = 0; c < 16; c++) {
        vga_set_color(c == 0 ? VGA_WHITE : VGA_BLACK, (enum vga_color)c);
        kprintf("  %2d  ", c);
        color(VGA_LIGHT_GREY);
        kprintf(" %s\n", names[c]);
    }
    return E_OK;
}

static err_t cmd_dmesg(int argc, char **argv)
{
    (void)argc; (void)argv;
    dmesg_print();
    return E_OK;
}

static err_t cmd_history(int argc, char **argv)
{
    (void)argc; (void)argv;
    for (int i = 0; i < hist.count; i++)
        kprintf(" %2d  %s\n", i + 1, history_get(&hist, i));
    return E_OK;
}

/* Alloue, remplit, vérifie puis libère un bloc : vérifie le tas et mesure sa vitesse. */
static err_t cmd_memtest(int argc, char **argv)
{
    u32 kb = 64, t0, t1;
    u8 *buf;
    usize size;

    if (argc > 2 || (argc == 2 && cmdline_parse_u32(argv[1], &kb) != E_OK))
        return usage("memtest [taille en Kio, 1 a 4096]");
    if (kb == 0 || kb > 4096)
        return fail_msg("Taille entre 1 et 4096 Kio.");

    size = (usize)kb * 1024;
    buf = kmalloc(size);
    if (!buf) {
        keprintf("kmalloc(%u Kio) a echoue : le tas est trop petit ou fragmente.\n", kb);
        return E_NOMEM;
    }

    t0 = pit_uptime_ms();
    for (u32 pass = 0; pass < 4; pass++) {
        static const u8 pattern[4] = { 0x00, 0xFF, 0xAA, 0x55 };

        memset(buf, pattern[pass], size);
        for (usize i = 0; i < size; i++) {
            if (buf[i] != pattern[pass]) {
                keprintf("ERREUR a l'offset %u : lu %#x, attendu %#x\n", (u32)i, buf[i], pattern[pass]);
                kfree(buf);
                return E_IO;
            }
        }
    }
    t1 = pit_uptime_ms();

    kfree(buf);
    kheap_check();
    kprintf("%u Kio : 4 motifs ecrits et relus sans erreur en %u ms. Tas integre.\n", kb, t1 - t0);
    return E_OK;
}

/* Provoque volontairement une erreur pour voir l'écran d'erreur. */
static err_t cmd_crash(int argc, char **argv)
{
    static const char *const kinds = "div0 | gp | ud | null | ro | panic | assert | doublefree | heap";

    if (argc != 2)
        return usage("crash <div0|gp|ud|null|ro|panic|assert|doublefree|heap>");

    if (strcmp(argv[1], "div0") == 0) {
        volatile int zero = 0;
        volatile int r = 1 / zero;
        (void)r;
    } else if (strcmp(argv[1], "gp") == 0) {
        __asm__ volatile ("movw $0x99, %%ax\n\tmovw %%ax, %%ds" : : : "eax");
    } else if (strcmp(argv[1], "ud") == 0) {
        __asm__ volatile ("ud2");
    } else if (strcmp(argv[1], "null") == 0) {
        *(volatile u32 *)0 = 1;                 /* page 0 absente : faute de page */
    } else if (strcmp(argv[1], "ro") == 0) {
        extern char kernel_start[];

        *(volatile u32 *)kernel_start = 0;      /* code du noyau en lecture seule : faute de page */
    } else if (strcmp(argv[1], "panic") == 0) {
        PANIC("Panique demandee depuis le shell");
    } else if (strcmp(argv[1], "assert") == 0) {
        ASSERT(argc > 100);
    } else if (strcmp(argv[1], "doublefree") == 0) {
        void *p = kmalloc(64);
        kfree(p);
        kfree(p);
    } else if (strcmp(argv[1], "heap") == 0) {
        u8 *p = kmalloc(32);
        u8 *q = kmalloc(32);

        if (!p || !q)
            return E_NOMEM;
        memset(p, 0x41, 32 + 16 + 4);           /* déborde sur l'en-tête du bloc suivant */
        kfree(q);
    } else {
        keprintf("Type inconnu '%s'. Types : %s\n", argv[1], kinds);
        return E_REPORTED;
    }
    return E_OK;
}

/* Avant d'éteindre ou de redémarrer : enregistre les fichiers modifiés si un disque PhoenOS est prêt. */
static void autosave_before_exit(void)
{
    err_t e;

    kprintf_redirect(NULL, NULL);
    if (persist_state() != PERSIST_READY || !persist_dirty())
        return;
    kprintf("Sauvegarde des fichiers sur le disque... ");
    e = persist_autosave();
    if (e == E_OK) {
        kprintf("ok (generation %u)\n", persist_get_info()->generation);
    } else {
        color(VGA_LIGHT_RED);
        kprintf("ECHEC (%s) : les modifications seront perdues.\n", err_str(e));
        color(VGA_LIGHT_GREY);
    }
}

static err_t cmd_reboot(int argc, char **argv)
{
    (void)argc; (void)argv;
    autosave_before_exit();
    kprintf("Redemarrage...\n");
    power_reboot();
}

static err_t cmd_shutdown(int argc, char **argv)
{
    (void)argc; (void)argv;
    autosave_before_exit();
    kprintf("Extinction...\n");
    power_shutdown();
}

static err_t cmd_halt(int argc, char **argv)
{
    (void)argc; (void)argv;
    autosave_before_exit();
    kprintf("Processeur arrete. Redemarrez la machine manuellement.\n");
    power_halt();
}

/* ------------------------------------------------------------------ */
/* Commandes fichiers                                                  */
/* ------------------------------------------------------------------ */

static void print_entry_name(const struct fs_node *n)
{
    if (n->type == FS_DIR) {
        color(VGA_LIGHT_CYAN);
        kprintf("%s/", n->name);
        color(VGA_LIGHT_GREY);
    } else {
        kprintf("%s", n->name);
    }
}

static void print_entry_long(const struct fs_node *n)
{
    if (n->type == FS_DIR)
        kprintf("d %8s  ", "-");
    else
        kprintf("- %8u  ", n->size);
    if (n->mtime) {
        struct rtc_time t;

        rtc_from_unix(n->mtime, &t);
        kprintf("%04u-%02u-%02u %02u:%02u  ", t.year, t.month, t.day, t.hour, t.min);
    } else {
        kprintf("%16s  ", "-");
    }
    print_entry_name(n);
    kprintf("\n");
}

static err_t cmd_ls(int argc, char **argv)
{
    struct fs_node *n;
    const char *target = ".";
    u32 flags;
    int first;
    usize col = 0;
    err_t e;

    if (parse_flags(argc, argv, "l", &flags, &first) != E_OK || argc - first > 1)
        return usage("ls [-l] [chemin]");
    if (first < argc)
        target = argv[first];

    e = fs_lookup(target, &n);
    if (e != E_OK)
        return fail_path("ls", target, e);

    if (n->type == FS_FILE) {
        if (flags & 1) {
            print_entry_long(n);
        } else {
            print_entry_name(n);
            kprintf("\n");
        }
        return E_OK;
    }

    for (const struct fs_node *c = n->child; c; c = c->next) {
        usize w;

        if (flags & 1) {
            print_entry_long(c);
            continue;
        }
        w = strlen(c->name) + (c->type == FS_DIR ? 1 : 0) + 2;
        if (col && col + w > 78) {
            kprintf("\n");
            col = 0;
        }
        print_entry_name(c);
        kprintf("  ");
        col += w;
    }
    if (col)
        kprintf("\n");
    return E_OK;
}

/* Copie des octets vers la sortie ; a l'ecran les octets de controle deviennent '.'. */
static void put_bytes(const u8 *d, u32 n)
{
    for (u32 k = 0; k < n; k++) {
        u8 c = d[k];

        if (!raw_output && c != '\n' && c != '\t' && (c < 32 || c == 127))
            c = '.';
        kprintf("%c", c);
    }
}

static err_t cmd_cat(int argc, char **argv)
{
    err_t result = E_OK;
    u32 flags, line = 1;
    int first, at_start = 1;

    if (parse_flags(argc, argv, "n", &flags, &first) != E_OK || (first >= argc && !has_stdin))
        return usage("cat [-n] [fichier...]   (sans fichier : lit l'entree d'un tube)");

    for (int i = first; i < argc || (i == first && first >= argc); i++) {
        const u8 *data;
        u32 size;

        if (first >= argc) {
            data = stdin_buf;
            size = stdin_len;
        } else {
            struct fs_node *n;
            err_t e = fs_lookup(argv[i], &n);

            if (e == E_OK && n->type != FS_FILE)
                e = E_ISDIR;
            if (e != E_OK) {
                result = fail_path("cat", argv[i], e);
                continue;
            }
            data = n->data;
            size = n->size;
        }
        for (u32 k = 0; k < size; k++) {
            u8 c = data[k];

            if ((flags & 1) && at_start)
                kprintf("%5u  ", line++);
            at_start = (c == '\n');
            put_bytes(&c, 1);
        }
        if (!raw_output && size && data[size - 1] != '\n') {
            kprintf("\n");
            at_start = 1;
        }
        if (first >= argc)
            break;
    }
    return result;
}

static err_t cmd_cd(int argc, char **argv)
{
    const char *p = argc == 2 ? argv[1] : "/";
    err_t e;

    if (argc > 2)
        return usage("cd [dossier]");
    e = fs_chdir(p);
    return e == E_OK ? E_OK : fail_path("cd", p, e);
}

static err_t cmd_pwd(int argc, char **argv)
{
    char path[FS_PATH_MAX];

    (void)argc; (void)argv;
    if (fs_path_of(fs_cwd(), path, sizeof(path)) != E_OK)
        return E_NAMETOOLONG;
    kprintf("%s\n", path);
    return E_OK;
}

static err_t cmd_mkdir(int argc, char **argv)
{
    u32 flags;
    int first;
    err_t result = E_OK;

    if (parse_flags(argc, argv, "p", &flags, &first) != E_OK || first >= argc)
        return usage("mkdir [-p] <dossier>...");
    for (int i = first; i < argc; i++) {
        err_t e = (flags & 1) ? fs_mkdir_p(argv[i]) : fs_mkdir(argv[i]);

        if (e != E_OK)
            result = fail_path("mkdir", argv[i], e);
    }
    return result;
}

static err_t cmd_touch(int argc, char **argv)
{
    err_t result = E_OK;

    if (argc < 2)
        return usage("touch <fichier>...");
    for (int i = 1; i < argc; i++) {
        struct fs_node *n;
        err_t e = fs_create(argv[i], &n);

        if (e != E_OK)
            result = fail_path("touch", argv[i], e);
    }
    return result;
}

static err_t cmd_rm(int argc, char **argv)
{
    u32 flags;
    int first;
    err_t result = E_OK;

    if (parse_flags(argc, argv, "r", &flags, &first) != E_OK || first >= argc)
        return usage("rm [-r] <chemin>...");
    for (int i = first; i < argc; i++) {
        err_t e = fs_remove(argv[i], (flags & 1) ? (FS_RM_DIR_OK | FS_RM_RECURSIVE) : 0);

        if (e != E_OK)
            result = fail_path("rm", argv[i], e);
    }
    return result;
}

static err_t cmd_rmdir(int argc, char **argv)
{
    err_t result = E_OK;

    if (argc < 2)
        return usage("rmdir <dossier>...");
    for (int i = 1; i < argc; i++) {
        struct fs_node *n;
        err_t e = fs_lookup(argv[i], &n);

        if (e == E_OK && n->type != FS_DIR)
            e = E_NOTDIR;
        if (e == E_OK)
            e = fs_remove(argv[i], FS_RM_DIR_OK);
        if (e != E_OK)
            result = fail_path("rmdir", argv[i], e);
    }
    return result;
}

static err_t cmd_cp(int argc, char **argv)
{
    err_t e;

    if (argc != 3)
        return usage("cp <source> <destination>");
    e = fs_copy(argv[1], argv[2]);
    return e == E_OK ? E_OK : fail_path("cp", e == E_NOENT || e == E_ISDIR ? argv[1] : argv[2], e);
}

static err_t cmd_mv(int argc, char **argv)
{
    err_t e;

    if (argc != 3)
        return usage("mv <source> <destination>");
    e = fs_rename(argv[1], argv[2]);
    return e == E_OK ? E_OK : fail_path("mv", e == E_NOENT || e == E_BUSY ? argv[1] : argv[2], e);
}

static void tree_print(const struct fs_node *dir, int level, int dirs_only, u32 *dirs, u32 *files)
{
    for (const struct fs_node *c = dir->child; c; c = c->next) {
        if (dirs_only && c->type != FS_DIR)
            continue;
        for (int i = 0; i < level; i++)
            kprintf("    ");
        kprintf("+-- ");
        print_entry_name(c);
        kprintf("\n");
        if (c->type == FS_DIR) {
            (*dirs)++;
            tree_print(c, level + 1, dirs_only, dirs, files);
        } else {
            (*files)++;
        }
    }
}

static err_t cmd_tree(int argc, char **argv)
{
    struct fs_node *n;
    const char *target = ".";
    u32 dirs = 0, files = 0, flags;
    int first;
    err_t e;

    if (parse_flags(argc, argv, "d", &flags, &first) != E_OK || argc - first > 1)
        return usage("tree [-d] [dossier]");
    if (first < argc)
        target = argv[first];
    e = fs_lookup(target, &n);
    if (e == E_OK && n->type != FS_DIR)
        e = E_NOTDIR;
    if (e != E_OK)
        return fail_path("tree", target, e);

    color(VGA_LIGHT_CYAN);
    kprintf("%s\n", n == fs_root() ? "/" : target);
    color(VGA_LIGHT_GREY);
    tree_print(n, 0, (int)(flags & 1), &dirs, &files);
    if (flags & 1)
        kprintf("%u dossier(s)\n", dirs);
    else
        kprintf("%u dossier(s), %u fichier(s)\n", dirs, files);
    return E_OK;
}

static err_t cmd_df(int argc, char **argv)
{
    struct fs_stats st;

    (void)argc; (void)argv;
    fs_get_stats(&st);
    kprintf("Systeme de fichiers en RAM (perdu a l'extinction)\n");
    kprintf("  fichiers : %u\n", st.files);
    kprintf("  dossiers : %u (racine comprise)\n", st.dirs);
    kprintf("  contenu  : %u octets\n", st.bytes);
    kprintf("  limites  : %u elements, %u Kio par fichier\n", FS_NODES_MAX, FS_FILE_MAX / 1024);
    if (persist_state() == PERSIST_READY)
        kprintf("  disque   : pret, generation %u%s\n", persist_get_info()->generation,
                persist_dirty() ? " (modifications non enregistrees : save)" : " (a jour)");
    else
        kprintf("  disque   : %s\n", persist_state() == PERSIST_NO_DISK ? "aucun" : "non formate (format oui)");
    return E_OK;
}

static err_t cmd_hexdump(int argc, char **argv)
{
    struct fs_node *n;
    u32 limit = 256, count;
    err_t e;

    if (argc < 2 || argc > 3 || (argc == 3 && cmdline_parse_u32(argv[2], &limit) != E_OK))
        return usage("hexdump <fichier> [octets]");
    e = fs_lookup(argv[1], &n);
    if (e == E_OK && n->type != FS_FILE)
        e = E_ISDIR;
    if (e != E_OK)
        return fail_path("hexdump", argv[1], e);

    count = n->size < limit ? n->size : limit;
    for (u32 off = 0; off < count; off += 16) {
        kprintf("%08x ", off);
        for (u32 i = 0; i < 16; i++) {
            if (i == 8)
                kprintf(" ");
            if (off + i < count)
                kprintf(" %02x", n->data[off + i]);
            else
                kprintf("   ");
        }
        kprintf("  |");
        for (u32 i = 0; i < 16 && off + i < count; i++) {
            u8 c = n->data[off + i];

            kprintf("%c", c >= 32 && c < 127 ? c : '.');
        }
        kprintf("|\n");
    }
    if (count < n->size)
        kprintf("... %u octet(s) de plus\n", n->size - count);
    return E_OK;
}

/* ------------------------------------------------------------------ */
/* Stockage                                                            */
/* ------------------------------------------------------------------ */

static err_t cmd_disks(int argc, char **argv)
{
    const struct persist_info *pi = persist_get_info();

    (void)argc; (void)argv;
    if (ata_count() == 0)
        kprintf("Aucun disque ATA detecte.\n");
    for (int i = 0; i < ata_count(); i++) {
        const struct ata_dev *d = ata_dev(i);

        kprintf(" %d  %s / %s  ", i, d->channel ? "secondaire" : "primaire  ", d->drive ? "esclave" : "maitre  ");
        if (d->atapi)
            kprintf("%s (non utilisable)\n", d->model);
        else
            kprintf("%s  %u Mio\n", d->model, d->sectors / 2048);
    }
    switch (persist_state()) {
    case PERSIST_NO_DISK:
        kprintf("Stockage PhoenOS : aucun disque utilisable (les fichiers sont perdus a l'extinction).\n");
        break;
    case PERSIST_UNFORMATTED:
        kprintf("Stockage PhoenOS : %s non formate. Tapez  format oui  pour l'utiliser.\n", pi->model);
        break;
    default:
        kprintf("Stockage PhoenOS : pret sur %s (generation %u, %u octets enregistres)%s\n",
                pi->model, pi->generation, pi->snapshot_bytes,
                persist_dirty() ? ", modifications non enregistrees" : "");
        break;
    }
    return E_OK;
}

static err_t cmd_format(int argc, char **argv)
{
    err_t e;

    if (argc < 2 || argc > 3 || strcmp(argv[1], "oui") != 0 || (argc == 3 && strcmp(argv[2], "oui") != 0))
        return usage("format oui   (efface la sauvegarde du disque ; oui oui : disque deja utilise)");
    if (persist_state() == PERSIST_NO_DISK)
        return fail_msg("Aucun disque utilisable (voir disks).");

    e = persist_format(argc == 3);
    if (e == E_BUSY)
        return fail_msg("Ce disque contient deja des donnees qui ne sont pas du PhoenFS. Pour l'effacer quand meme : format oui oui");
    if (e != E_OK)
        return e;
    kprintf("Disque formate. Utilisez save pour enregistrer vos fichiers (shutdown et reboot le font seuls).\n");
    return E_OK;
}

static err_t cmd_save(int argc, char **argv)
{
    err_t e;

    (void)argv;
    if (argc != 1)
        return usage("save");
    if (persist_state() == PERSIST_UNFORMATTED) {                /* disque neuf : on le prepare tout seul */
        e = persist_format(0);
        if (e == E_BUSY)
            return fail_msg("Ce disque contient deja d'autres donnees : PhoenOS n'y touche pas. Pour l'effacer et l'utiliser : format oui oui");
        if (e != E_OK)
            return e;
        kprintf("Disque neuf : prepare automatiquement pour PhoenOS.\n");
    }
    if (persist_state() != PERSIST_READY)
        return fail_msg("Aucun disque utilisable (voir disks).");
    e = persist_save();
    if (e == E_FBIG)
        return fail_msg("Les fichiers sont trop volumineux pour l'emplacement de sauvegarde du disque (8 Mio maximum).");
    if (e != E_OK)
        return e;
    kprintf("Sauvegarde %u enregistree (%u octets).\n", persist_get_info()->generation,
            persist_get_info()->snapshot_bytes);
    return E_OK;
}

static err_t cmd_load(int argc, char **argv)
{
    struct initrd_stats st;
    err_t e;

    (void)argv;
    if (argc != 1)
        return usage("load");
    if (persist_state() != PERSIST_READY)
        return fail_msg(persist_state() == PERSIST_NO_DISK ? "Aucun disque utilisable (voir disks)."
                                                           : "Disque non formate : tapez  format oui");
    e = persist_load(&st);
    if (e != E_OK)
        return e;
    kprintf("Sauvegarde %u relue : %u fichier(s), %u octets (les fichiers de meme nom ont ete remplaces).\n",
            persist_get_info()->generation, st.files, st.bytes);
    persist_mark_clean();
    return E_OK;
}

/*
 * man : lit /docs/manuel.txt (fourni par l'initrd). Le fichier est découpé en sections ;
 * une section commence par une ligne "== nom" et dure jusqu'à la ligne "== " suivante.
 */
/* ------------------------------------------------------------------ */
/* Date et outils texte                                                */
/* ------------------------------------------------------------------ */

static err_t cmd_date(int argc, char **argv)
{
    struct rtc_time t;
    u32 flags;
    int first;
    err_t e;

    if (parse_flags(argc, argv, "s", &flags, &first) != E_OK || first != argc)
        return usage("date [-s]");
    e = rtc_read(&t);
    if (e != E_OK)
        return fail_msg("Horloge materielle illisible (pile vide ou machine sans horloge).");
    if (flags & 1) {
        kprintf("%u\n", rtc_to_unix(&t));
        return E_OK;
    }
    kprintf("%s %u %s %u, %02u:%02u:%02u\n", rtc_day_name(t.wday), t.day, rtc_month_name(t.month), t.year,
            t.hour, t.min, t.sec);
    return E_OK;
}

/* Source d'une commande texte : un fichier, ou l'entree standard si path == NULL. */
static err_t text_source(const char *cmd, const char *path, const u8 **data, u32 *size)
{
    struct fs_node *n;
    err_t e;

    if (!path) {
        if (!has_stdin)
            return fail_msg("Pas d'entree : donnez un fichier ou utilisez un tube (cmd | ...).");
        *data = stdin_buf;
        *size = stdin_len;
        return E_OK;
    }
    e = fs_lookup(path, &n);
    if (e == E_OK && n->type != FS_FILE)
        e = E_ISDIR;
    if (e != E_OK)
        return fail_path(cmd, path, e);
    *data = n->data;
    *size = n->size;
    return E_OK;
}

/* Ligne suivante : [*start, *start + *len) sans le '\n'. Renvoie 0 a la fin. */
static int next_line(const u8 *d, u32 size, u32 *pos, u32 *start, u32 *len)
{
    u32 p = *pos;

    if (p >= size)
        return 0;
    *start = p;
    while (p < size && d[p] != '\n')
        p++;
    *len = p - *start;
    *pos = p < size ? p + 1 : p;
    return 1;
}

static void put_line(const u8 *d, u32 len)
{
    put_bytes(d, len);
    kprintf("\n");
}

/* -N en premier argument (head, tail). */
static err_t parse_count(const char *usage_text, int argc, char **argv, u32 *count, const char **file)
{
    int i = 1;

    *count = 10;
    *file = NULL;
    if (i < argc && argv[i][0] == '-' && argv[i][1]) {
        u32 v = 0;

        for (const char *c = argv[i] + 1; *c; c++) {
            if (*c < '0' || *c > '9' || v > 99999999u)
                goto bad;
            v = v * 10 + (u32)(*c - '0');
        }
        *count = v;
        i++;
    }
    if (argc - i > 1)
        goto bad;
    if (i < argc)
        *file = argv[i];
    return E_OK;
bad:
    return usage(usage_text);
}

static err_t cmd_head(int argc, char **argv)
{
    const u8 *d;
    const char *file;
    u32 size, count, pos = 0, st, len;
    err_t e = parse_count("head [-N] [fichier]", argc, argv, &count, &file);

    if (e != E_OK)
        return e;
    e = text_source("head", file, &d, &size);
    if (e != E_OK)
        return e;
    while (count-- && next_line(d, size, &pos, &st, &len))
        put_line(d + st, len);
    return E_OK;
}

static err_t cmd_tail(int argc, char **argv)
{
    const u8 *d;
    const char *file;
    u32 size, count, total = 0, pos = 0, st, len, skip, idx = 0;
    err_t e = parse_count("tail [-N] [fichier]", argc, argv, &count, &file);

    if (e != E_OK)
        return e;
    e = text_source("tail", file, &d, &size);
    if (e != E_OK)
        return e;
    while (next_line(d, size, &pos, &st, &len))
        total++;
    skip = total > count ? total - count : 0;
    pos = 0;
    while (next_line(d, size, &pos, &st, &len))
        if (idx++ >= skip)
            put_line(d + st, len);
    return E_OK;
}

static int is_space(u8 c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static err_t cmd_wc(int argc, char **argv)
{
    u32 flags, tl = 0, tw = 0, tc = 0;
    int first, files;
    err_t result = E_OK;

    if (parse_flags(argc, argv, "lwc", &flags, &first) != E_OK)
        return usage("wc [-lwc] [fichier...]");
    if (!flags)
        flags = 7;
    files = argc - first;
    for (int i = first; i < argc || (i == first && files == 0); i++) {
        const u8 *d;
        u32 size, l = 0, w = 0, in_word = 0;
        err_t e = text_source("wc", files ? argv[i] : NULL, &d, &size);

        if (e != E_OK) {
            result = e;
            if (!files)
                break;
            continue;
        }
        for (u32 k = 0; k < size; k++) {
            if (d[k] == '\n')
                l++;
            if (is_space(d[k])) {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                w++;
            }
        }
        if (flags & 1) kprintf("%7u ", l);
        if (flags & 2) kprintf("%7u ", w);
        if (flags & 4) kprintf("%7u ", size);
        if (files)
            kprintf("%s", argv[i]);
        kprintf("\n");
        tl += l; tw += w; tc += size;
        if (!files)
            break;
    }
    if (files > 1) {
        if (flags & 1) kprintf("%7u ", tl);
        if (flags & 2) kprintf("%7u ", tw);
        if (flags & 4) kprintf("%7u ", tc);
        kprintf("total\n");
    }
    return result == E_OK ? E_OK : E_REPORTED;
}

static u8 lower(u8 c)
{
    return (c >= 'A' && c <= 'Z') ? (u8)(c + 32) : c;
}

static int line_contains(const u8 *l, u32 ll, const char *pat, u32 pl, int icase)
{
    if (pl > ll)
        return 0;
    for (u32 i = 0; i + pl <= ll; i++) {
        u32 k = 0;

        while (k < pl && (icase ? lower(l[i + k]) == lower((u8)pat[k]) : l[i + k] == (u8)pat[k]))
            k++;
        if (k == pl)
            return 1;
    }
    return 0;
}

static err_t cmd_grep(int argc, char **argv)
{
    u32 flags;
    int first, files;
    err_t result = E_OK;
    const char *pat;
    u32 pl;

    if (parse_flags(argc, argv, "inv", &flags, &first) != E_OK || first >= argc)
        return usage("grep [-inv] <motif> [fichier...]");
    pat = argv[first++];
    pl = (u32)strlen(pat);
    files = argc - first;
    for (int i = first; i < argc || (i == first && files == 0); i++) {
        const u8 *d;
        u32 size, pos = 0, st, len, no = 0;
        err_t e = text_source("grep", files ? argv[i] : NULL, &d, &size);

        if (e != E_OK) {
            result = e;
            if (!files)
                break;
            continue;
        }
        while (next_line(d, size, &pos, &st, &len)) {
            int hit = line_contains(d + st, len, pat, pl, (flags & 1) != 0);

            no++;
            if (hit == ((flags & 4) != 0))
                continue;
            if (files > 1)
                kprintf("%s:", argv[i]);
            if (flags & 2)
                kprintf("%u:", no);
            put_line(d + st, len);
        }
        if (!files)
            break;
    }
    return result == E_OK ? E_OK : E_REPORTED;
}

#define SORT_MAX_LINES 16384

struct sline {
    const u8 *p;
    u32 n;
};

static int sline_cmp(const struct sline *a, const struct sline *b)
{
    u32 m = a->n < b->n ? a->n : b->n;
    int r = memcmp(a->p, b->p, m);

    if (r)
        return r;
    return a->n < b->n ? -1 : (a->n > b->n ? 1 : 0);
}

/* Tri fusion stable (pas de recursion : la pile du noyau est petite). */
static void sort_lines(struct sline *a, struct sline *tmp, u32 n, int reverse)
{
    for (u32 w = 1; w < n; w *= 2) {
        for (u32 lo = 0; lo < n; lo += 2 * w) {
            u32 mid = lo + w < n ? lo + w : n;
            u32 hi = lo + 2 * w < n ? lo + 2 * w : n;
            u32 i = lo, j = mid, k = lo;

            while (i < mid && j < hi) {
                int c = sline_cmp(&a[j], &a[i]);

                /* on prend a[j] seulement s'il est strictement avant a[i] (stabilite) */
                if (reverse ? c > 0 : c < 0)
                    tmp[k++] = a[j++];
                else
                    tmp[k++] = a[i++];
            }
            while (i < mid)
                tmp[k++] = a[i++];
            while (j < hi)
                tmp[k++] = a[j++];
        }
        memcpy(a, tmp, n * sizeof(*a));
    }
}

static err_t cmd_sort(int argc, char **argv)
{
    const u8 *d;
    u32 flags, size, pos = 0, st, len, n = 0;
    int first;
    struct sline *lines, *tmp;
    err_t e;

    if (parse_flags(argc, argv, "ru", &flags, &first) != E_OK || argc - first > 1)
        return usage("sort [-ru] [fichier]");
    e = text_source("sort", first < argc ? argv[first] : NULL, &d, &size);
    if (e != E_OK)
        return e;

    lines = kmalloc(2 * SORT_MAX_LINES * sizeof(*lines));
    if (!lines)
        return E_NOMEM;
    tmp = lines + SORT_MAX_LINES;
    while (next_line(d, size, &pos, &st, &len)) {
        if (n == SORT_MAX_LINES) {
            kfree(lines);
            return fail_msg("sort: trop de lignes (maximum 16384).");
        }
        lines[n].p = d + st;
        lines[n].n = len;
        n++;
    }
    sort_lines(lines, tmp, n, (flags & 1) != 0);
    for (u32 i = 0; i < n; i++) {
        if ((flags & 2) && i && sline_cmp(&lines[i], &lines[i - 1]) == 0)
            continue;
        put_line(lines[i].p, lines[i].n);
    }
    kfree(lines);
    return E_OK;
}

#define MORE_ROWS 23                    /* lignes d'ecran par page (25 moins l'invite et la ligne de commande) */

static err_t cmd_more(int argc, char **argv)
{
    static const char prompt_text[] = "-- Suite : Espace = page, Entree = ligne, q = quitter --";
    const u8 *d;
    u32 size, pos = 0, st, len, rows = 0;
    err_t e;

    if (argc > 2)
        return usage("more [fichier]");
    e = text_source("more", argc == 2 ? argv[1] : NULL, &d, &size);
    if (e != E_OK)
        return e;

    while (next_line(d, size, &pos, &st, &len)) {
        u32 need = len ? (len + 79) / 80 : 1;           /* lignes d'ecran occupees (retour automatique) */

        if (!raw_output && rows && rows + need > MORE_ROWS) {
            int k;

            color(VGA_YELLOW);
            kprintf("%s", prompt_text);
            color(VGA_LIGHT_GREY);
            do {
                k = keyboard_getc();
            } while (k != ' ' && k != '\n' && k != 'q' && k != 'Q' && k != 3);
            erase_line(sizeof(prompt_text) - 1);
            if (k == 'q' || k == 'Q' || k == 3)
                return E_OK;
            rows = (k == '\n') ? MORE_ROWS - need : 0;    /* Entree : une ligne de plus seulement */
        }
        put_line(d + st, len);
        rows += need;
    }
    return E_OK;
}

#define MANUAL_PATH "/docs/manuel.txt"

static int is_section_line(const u8 *p, u32 left)
{
    return left >= 3 && p[0] == '=' && p[1] == '=' && p[2] == ' ';
}

static err_t cmd_man(int argc, char **argv)
{
    struct fs_node *n;
    const u8 *d;
    u32 size, pos = 0;
    int found = 0, printing = 0;
    err_t e;

    if (argc > 2)
        return usage("man [commande]");
    e = fs_lookup(MANUAL_PATH, &n);
    if (e != E_OK || n->type != FS_FILE)
        return fail_path("man", MANUAL_PATH, e != E_OK ? e : E_ISDIR);
    d = n->data;
    size = n->size;

    if (argc == 1)
        kprintf("Sujets du manuel (man <sujet>) :\n");

    while (pos < size) {
        u32 end = pos;

        while (end < size && d[end] != '\n')
            end++;

        if (is_section_line(d + pos, size - pos)) {
            u32 ns = pos + 3, ne = ns;

            while (ne < end && d[ne] != ' ' && d[ne] != '\r')
                ne++;
            if (argc == 1) {
                kprintf(" ");
                for (u32 i = ns; i < ne; i++)
                    kprintf("%c", d[i]);
                if (ne < end) {
                    kprintf("  ");
                    while (ne < end && d[ne] == ' ')
                        ne++;
                    for (u32 i = ne; i < end; i++)
                        kprintf("%c", d[i]);
                }
                kprintf("\n");
            } else {
                if (printing)
                    break;                      /* section suivante : fin */
                printing = (strlen(argv[1]) == ne - ns && memcmp(d + ns, argv[1], ne - ns) == 0);
                if (printing) {
                    found = 1;
                    color(VGA_YELLOW);
                    for (u32 i = ns; i < end; i++)
                        kprintf("%c", d[i]);
                    kprintf("\n");
                    color(VGA_LIGHT_GREY);
                }
            }
        } else if (printing) {
            for (u32 i = pos; i < end; i++)
                if (d[i] != '\r')
                    kprintf("%c", d[i]);
            kprintf("\n");
        }
        pos = end + 1;
    }

    if (argc == 2 && !found) {
        color(VGA_LIGHT_RED);
        keprintf("man: pas de manuel pour '%s' (essayez man)\n", argv[1]);
        color(VGA_LIGHT_GREY);
        return E_REPORTED;
    }
    return E_OK;
}

/* ------------------------------------------------------------------ */
/* Exécution d'une ligne, redirection                                  */
/* ------------------------------------------------------------------ */

struct capture {
    char *buf;
    u32   len, cap;
    int   overflow;
    int   nomem;
};

static void capture_sink(char c, void *ctx)
{
    struct capture *cp = ctx;

    if (cp->overflow || cp->nomem)
        return;
    if (cp->len == cp->cap) {
        u32 newcap = cp->cap ? cp->cap * 2 : 1024;
        char *nb;

        if (newcap > CAPTURE_MAX) {
            cp->overflow = 1;
            return;
        }
        nb = kmalloc(newcap);
        if (!nb) {
            cp->nomem = 1;
            return;
        }
        if (cp->len)
            memcpy(nb, cp->buf, cp->len);
        kfree(cp->buf);
        cp->buf = nb;
        cp->cap = newcap;
    }
    cp->buf[cp->len++] = c;
}

/* Lance le programme utilisateur `path` (fichier ELF). argv[0] = nom affiche. */
static err_t exec_program(const char *path, int argc, char **argv)
{
    struct fs_node *n;
    struct user_result res;
    const u8 *image;
    u32 size;
    err_t e = fs_lookup(path, &n);

    if (e == E_OK && n->type != FS_FILE)
        e = E_ISDIR;
    if (e != E_OK)
        return e;
    image = n->data;
    size = n->size;
    if (poe_is(image, size)) {                              /* application .poe : on verifie puis on lance son programme */
        struct poe_info info;
        enum poe_status s = poe_parse(image, size, &info);

        if (s != POE_OK) {
            color(VGA_LIGHT_RED);
            keprintf("%s : %s\n", path, poe_status_text(s));
            color(VGA_LIGHT_GREY);
            return E_REPORTED;
        }
        image = info.payload;
        size = info.payload_len;
    }
    if (bg_mode) {
        u32 pid = 0;

        e = user_spawn_bg(image, size, argv[0], argc, argv, &pid);
        if (e == E_OK) {
            color(VGA_YELLOW);
            kprintf("[%u] %s en arriere-plan (ps pour la liste, kill %u pour l'arreter)\n", pid, argv[0], pid);
            color(VGA_LIGHT_GREY);
            return E_OK;
        }
        res.exit_code = 0;
        res.killed = 0;
    } else {
        user_set_stdin(stdin_buf, stdin_len, has_stdin);
        e = user_exec(image, size, argv[0], argc, argv, &res);
    }
    if (e == E_NOSYS) {
        fail_msg("Programmes utilisateur indisponibles (zone memoire non reservee au demarrage).");
        return E_REPORTED;
    }
    if (e == E_NOEXEC) {
        color(VGA_LIGHT_RED);
        keprintf("%s : pas un programme PhoenOS valide (ELF 32 bits statique attendu)\n", path);
        color(VGA_LIGHT_GREY);
        return E_REPORTED;
    }
    if (e == E_2BIG) {
        fail_msg("Arguments trop longs pour un programme (16 mots, 4 Kio).");
        return E_REPORTED;
    }
    if (e != E_OK)
        return e;
    if (!res.killed && res.exit_code != 0) {
        color(VGA_YELLOW);
        keprintf("%s : code de sortie %d\n", argv[0], res.exit_code);
        color(VGA_LIGHT_GREY);
    }
    return E_OK;
}

static const char *base_name(const char *path)
{
    const char *b = path;

    for (const char *c = path; *c; c++)
        if (*c == '/')
            b = c + 1;
    return b;
}

/* Nom d'application : "calc.poe" -> "calc". */
static void poe_app_name(const char *path, char *dst, usize size)
{
    const char *b = base_name(path);
    usize n = strlen(b);

    if (n > 4 && strcmp(b + n - 4, ".poe") == 0)
        n -= 4;
    if (n >= size)
        n = size - 1;
    memcpy(dst, b, n);
    dst[n] = 0;
}

/* Cherche une application : chemin tel quel, avec ".poe", puis dans /apps. */
static err_t find_poe(const char *arg, char *path, usize size)
{
    static const struct { const char *pre, *post; } forms[] = { { "", "" }, { "", ".poe" }, { "/apps/", "" }, { "/apps/", ".poe" } };
    struct fs_node *n;

    if (strlen(arg) + 12 > size)
        return E_NAMETOOLONG;
    for (int i = 0; i < 4; i++) {
        if (i >= 2 && strchr(arg, '/'))
            break;
        strcpy(path, forms[i].pre);
        strcpy(path + strlen(path), arg);
        strcpy(path + strlen(path), forms[i].post);
        if (fs_lookup(path, &n) == E_OK && n->type == FS_FILE)
            return E_OK;
    }
    return E_NOENT;
}

static err_t cmd_exe(int argc, char **argv)
{
    char path[128], app[40];
    char *pargv[MAX_ARGS + 1];
    struct fs_node *n;
    err_t e;

    if (argc < 2)
        return usage("exe <application>[.poe] [arguments...]");
    e = find_poe(argv[1], path, sizeof path);
    if (e != E_OK)
        return fail_path("exe", argv[1], e);
    fs_lookup(path, &n);
    if (!poe_is(n->data, n->size)) {
        color(VGA_LIGHT_RED);
        keprintf("exe : %s n'est pas une application .poe (pour un programme ordinaire : run)\n", path);
        color(VGA_LIGHT_GREY);
        return E_REPORTED;
    }
    poe_app_name(path, app, sizeof app);
    for (int i = 1; i < argc; i++)
        pargv[i - 1] = argv[i];
    pargv[0] = app;
    e = exec_program(path, argc - 1, pargv);
    return e;
}

static err_t cmd_poeinfo(int argc, char **argv)
{
    char path[128];
    struct fs_node *n;
    struct poe_info info;
    enum poe_status s;
    err_t e;

    if (argc != 2)
        return usage("poeinfo <application>[.poe]");
    e = find_poe(argv[1], path, sizeof path);
    if (e != E_OK)
        return fail_path("poeinfo", argv[1], e);
    fs_lookup(path, &n);
    s = poe_parse(n->data, n->size, &info);
    if (s == POE_NOT_POE || s == POE_TRUNCATED || s == POE_BAD_HEADER) {
        color(VGA_LIGHT_RED);
        keprintf("%s : %s\n", path, poe_status_text(s));
        color(VGA_LIGHT_GREY);
        return E_REPORTED;
    }
    kprintf("Fichier        : %s\n", path);
    kprintf("Application    : %s %s\n", info.name, info.version);
    kprintf("Auteur         : %s\n", info.author);
    kprintf("API requise    : %u (ce PhoenOS : %u)\n", info.min_api, (u32)POE_API_LEVEL);
    kprintf("Exige          : %s%s%s\n", info.flags ? "" : "rien de special",
            (info.flags & POE_NEEDS_GUI) ? "interface graphique " : "", (info.flags & POE_NEEDS_NET) ? "reseau" : "");
    kprintf("Programme      : %u octets, somme de controle %s\n", info.payload_len, s == POE_BAD_CRC ? "FAUSSE" : "exacte");
    kprintf("Etat           : %s\n", poe_status_text(s));
    return E_OK;
}

static err_t cmd_run(int argc, char **argv)
{
    char *pargv[MAX_ARGS + 1];
    err_t e;

    if (argc < 2)
        return usage("run <programme> [arguments...]");
    for (int i = 1; i < argc; i++)
        pargv[i - 1] = argv[i];
    pargv[0] = (char *)base_name(argv[1]);
    if (!pargv[0][0])
        return fail_path("run", argv[1], E_ISDIR);
    if (argv[1][0] != '/' && !strchr(argv[1], '/')) {   /* nom seul : cherche dans /bin */
        char path[64];

        if (strlen(argv[1]) > 40)
            return fail_path("run", argv[1], E_NAMETOOLONG);
        strcpy(path, "/bin/");
        strcpy(path + 5, argv[1]);
        e = exec_program(path, argc - 1, pargv);
    } else {
        e = exec_program(argv[1], argc - 1, pargv);
    }
    if (e == E_NOENT || e == E_ISDIR)
        return fail_path("run", argv[1], e);
    return e;
}

static err_t run_command(int argc, char **argv)
{
    for (int i = 0; i < COMMAND_COUNT; i++) {
        if (strcmp(commands[i].name, argv[0]) == 0) {
            err_t e;

            if (bg_mode && strcmp(argv[0], "run") != 0 && strcmp(argv[0], "exe") != 0)
                return fail_msg("& : seulement pour les programmes (run, exe ou un nom de programme).");
            e = commands[i].fn(argc, argv);

            if (e != E_OK && e != E_REPORTED) {
                color(VGA_LIGHT_RED);
                keprintf("%s : echec (%s)\n", argv[0], err_str(e));
                color(VGA_LIGHT_GREY);
            }
            return e;
        }
    }
    if (strlen(argv[0]) <= 40 && !strchr(argv[0], '/')) {      /* programme dans /bin ? */
        char path[64];
        err_t e;

        strcpy(path, "/bin/");
        strcpy(path + 5, argv[0]);
        e = exec_program(path, argc, argv);
        if (e == E_NOENT && strlen(argv[0]) <= 40) {            /* sinon : application /apps/<nom>.poe */
            strcpy(path, "/apps/");
            strcpy(path + 6, argv[0]);
            strcpy(path + 6 + strlen(argv[0]), ".poe");
            e = exec_program(path, argc, argv);
        }
        if (e != E_NOENT && e != E_ISDIR) {
            if (e != E_OK && e != E_REPORTED) {
                color(VGA_LIGHT_RED);
                keprintf("%s : echec (%s)\n", argv[0], err_str(e));
                color(VGA_LIGHT_GREY);
            }
            return e;
        }
    }
    color(VGA_LIGHT_RED);
    keprintf("Commande inconnue : %s", argv[0]);
    color(VGA_LIGHT_GREY);
    keprintf("  (tapez help)\n");
    return E_REPORTED;
}

static int is_redirect_token(const char *s)
{
    return strcmp(s, ">") == 0 || strcmp(s, ">>") == 0;
}

/* Execute une commande ; si cap != NULL, sa sortie est capturee (redirection ou tube). */
static void run_stage(int argc, char **argv, struct capture *cap)
{
    if (cap) {
        memset(cap, 0, sizeof(*cap));
        raw_output = 1;
        kprintf_redirect(capture_sink, cap);
    }
    run_command(argc, argv);
    if (cap) {
        kprintf_redirect(NULL, NULL);
        raw_output = 0;
    }
    color(VGA_LIGHT_GREY);
}

static void execute(char *line)
{
    char *argv[MAX_ARGS + 1];
    u8 quoted[MAX_ARGS + 1];
    int argc = cmdline_split_ex(line, argv, MAX_ARGS, quoted);
    int starts[MAX_STAGES], counts[MAX_STAGES], stages = 0, s = 0;
    const char *target = NULL;
    int append = 0;
    struct fs_node *node = NULL;
    struct capture cap;
    char *held = NULL;                          /* sortie du tube precedent (a liberer) */
    err_t e;

    if (argc < 0) {
        fail_msg(argc == E_2BIG ? "Trop d'arguments (maximum 16)." : "Guillemet non ferme.");
        return;
    }
    if (argc == 0)
        return;

    /* Arriere-plan :  programme ... &  */
    if (!quoted[argc - 1] && strcmp(argv[argc - 1], "&") == 0) {
        argc--;
        argv[argc] = NULL;
        if (argc == 0) {
            fail_msg("Commande manquante avant &.");
            return;
        }
        for (int i = 0; i < argc; i++)
            if (!quoted[i] && (is_redirect_token(argv[i]) || strcmp(argv[i], "|") == 0)) {
                fail_msg("& ne se combine pas avec les tubes ni les redirections.");
                return;
            }
        bg_mode = 1;
        run_command(argc, argv);
        bg_mode = 0;
        color(VGA_LIGHT_GREY);
        return;
    }

    /* Redirection : uniquement en fin de ligne :  commande ... > fichier */
    if (argc >= 2 && !quoted[argc - 2] && is_redirect_token(argv[argc - 2])) {
        target = argv[argc - 1];
        append = argv[argc - 2][1] == '>';
        argc -= 2;
        argv[argc] = NULL;
        if (argc == 0) {
            fail_msg("Commande manquante avant la redirection.");
            return;
        }
    }
    for (int i = 0; i < argc; i++) {
        if (!quoted[i] && is_redirect_token(argv[i])) {
            fail_msg("Redirection invalide : ecrire  commande > fichier  (en fin de ligne).");
            return;
        }
    }

    /* Tubes : "|" seul, sans guillemets, separe les commandes. */
    for (int i = 0; i <= argc; i++) {
        if (i == argc || (!quoted[i] && strcmp(argv[i], "|") == 0)) {
            if (i == s) {
                fail_msg("Commande manquante autour du tube |.");
                return;
            }
            if (stages == MAX_STAGES) {
                fail_msg("Trop de tubes (maximum 4 commandes reliees).");
                return;
            }
            starts[stages] = s;
            counts[stages] = i - s;
            stages++;
            s = i + 1;
        }
    }

    if (target) {
        /* Le fichier est cree AVANT les commandes : erreur claire si le chemin est faux. */
        e = fs_create(target, &node);
        if (e != E_OK) {
            fail_path("redirection", target, e);
            return;
        }
    }

    for (int st = 0; st < stages; st++) {
        int last = (st == stages - 1);
        char *out = NULL;
        u32 out_len = 0;
        int nomem = 0, overflow = 0;

        argv[starts[st] + counts[st]] = NULL;
        if (last && !target) {
            run_stage(counts[st], argv + starts[st], NULL);
        } else {
            run_stage(counts[st], argv + starts[st], &cap);
            out = cap.buf;
            out_len = cap.len;
            nomem = cap.nomem;
            overflow = cap.overflow;
        }
        kfree(held);                            /* l'entree de cette commande n'est plus utile */
        held = NULL;
        has_stdin = 0;
        stdin_buf = NULL;
        stdin_len = 0;

        if (last) {
            if (target) {
                if (nomem) {
                    fail_msg("Memoire insuffisante : sortie perdue.");
                } else {
                    e = append ? fs_append(node, out, out_len) : fs_write(node, out, out_len);
                    if (e != E_OK)
                        fail_path("redirection", target, e);
                    else if (overflow)
                        fail_msg("Sortie tronquee a 128 Kio.");
                }
                kfree(out);
            }
        } else {
            if (nomem) {
                fail_msg("Memoire insuffisante : tube interrompu.");
                kfree(out);
                return;
            }
            if (overflow)
                fail_msg("Sortie intermediaire tronquee a 128 Kio.");
            held = out;
            stdin_buf = (const u8 *)out;
            stdin_len = out_len;
            has_stdin = 1;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Saisie d'une ligne                                                  */
/* ------------------------------------------------------------------ */

static void prompt(void)
{
    char path[FS_PATH_MAX];

    if (fs_path_of(fs_cwd(), path, sizeof(path)) != E_OK)
        memcpy(path, "?", 2);
    color(VGA_LIGHT_GREEN);
    kprintf("phoenos");
    color(VGA_LIGHT_GREY);
    kprintf(":");
    color(VGA_LIGHT_CYAN);
    kprintf("%s", path);
    color(VGA_LIGHT_GREY);
    kprintf("> ");
}

static void erase_line(usize len)
{
    while (len--)
        kprintf("\b \b");
}

/* Remplace le texte affiché (et mémorisé) par `text`. */
static void replace_line(char *line, usize *len, const char *text)
{
    erase_line(*len);
    *len = strlen(text);
    memcpy(line, text, *len + 1);
    kprintf("%s", line);
}

static void append_text(char *line, usize *len, const char *text, usize n)
{
    for (usize i = 0; i < n && *len + 1 < LINE_MAX; i++) {
        line[(*len)++] = text[i];
        kprintf("%c", text[i]);
    }
    line[*len] = '\0';
}

static int starts_with(const char *s, const char *prefix, usize plen)
{
    return strlen(s) >= plen && memcmp(s, prefix, plen) == 0;
}

/* Complétion du nom de commande (premier mot). */
static void complete_command(char *line, usize *len)
{
    int matches = 0, last = -1;

    for (int i = 0; i < COMMAND_COUNT; i++) {
        if (starts_with(commands[i].name, line, *len)) {
            matches++;
            last = i;
        }
    }
    if (matches == 1) {
        const char *name = commands[last].name;

        append_text(line, len, name + *len, strlen(name) - *len);
        append_text(line, len, " ", 1);
    } else if (matches > 1) {
        kprintf("\n");
        for (int i = 0; i < COMMAND_COUNT; i++)
            if (starts_with(commands[i].name, line, *len))
                kprintf("%s  ", commands[i].name);
        kprintf("\n");
        prompt();
        kprintf("%s", line);
    }
}

/* Complétion d'un chemin (dernier mot) dans le système de fichiers. */
static void complete_path(char *line, usize *len, usize word_start)
{
    const char *word = line + word_start;
    usize wlen = *len - word_start, split = 0, blen, common = 0;
    char dirpart[FS_PATH_MAX];
    const char *base;
    struct fs_node *dir = NULL, *last = NULL;
    int matches = 0;

    if (wlen >= FS_PATH_MAX)
        return;
    for (usize i = 0; i < wlen; i++) {
        if (word[i] == '"')
            return;
        if (word[i] == '/')
            split = i + 1;
    }
    memcpy(dirpart, word, split);
    dirpart[split] = '\0';
    base = word + split;
    blen = wlen - split;

    if (split == 0)
        dir = fs_cwd();
    else if (fs_lookup(dirpart, &dir) != E_OK || dir->type != FS_DIR)
        return;

    for (struct fs_node *c = dir->child; c; c = c->next) {
        usize nl = strlen(c->name);

        if (!starts_with(c->name, base, blen))
            continue;
        if (matches == 0) {
            common = nl;
        } else {
            usize k = 0;

            while (k < common && k < nl && c->name[k] == last->name[k])
                k++;
            common = k;
        }
        matches++;
        last = c;
    }

    if (matches == 1) {
        append_text(line, len, last->name + blen, strlen(last->name) - blen);
        append_text(line, len, last->type == FS_DIR ? "/" : " ", 1);
    } else if (matches > 1) {
        if (common > blen) {
            append_text(line, len, last->name + blen, common - blen);
            return;
        }
        kprintf("\n");
        for (const struct fs_node *c = dir->child; c; c = c->next) {
            if (starts_with(c->name, base, blen)) {
                print_entry_name(c);
                kprintf("  ");
            }
        }
        kprintf("\n");
        prompt();
        kprintf("%s", line);
    }
}

static void complete(char *line, usize *len)
{
    usize word_start = 0;

    for (usize i = 0; i < *len; i++)
        if (line[i] == ' ' || line[i] == '\t')
            word_start = i + 1;
    if (word_start == 0)
        complete_command(line, len);
    else
        complete_path(line, len, word_start);
}

/* Lit une ligne au clavier. Renvoie quand l'utilisateur appuie sur Entrée. */
static void read_line(char *line, usize *len_out)
{
    char draft[LINE_MAX];
    usize len = 0;

    line[0] = draft[0] = '\0';
    history_add(&hist, "");                     /* remet la navigation à la fin */

    for (;;) {
        int key = keyboard_getc();

        if (key == KEY_PGUP || key == 2) {      /* page precedente : PageHaut, Ctrl+B (voir man general) */
            vga_scroll_view(SCROLL_STEP);
            continue;
        }
        if (key == KEY_PGDN || key == 6) {      /* page suivante : PageBas, Ctrl+F */
            vga_scroll_view(-SCROLL_STEP);
            continue;
        }
        vga_scroll_reset();                     /* toute autre touche ramène à la saisie */

        switch (key) {
        case '\n':
            kprintf("\n");
            *len_out = len;
            return;

        case '\b':
            if (len > 0) {
                len--;
                line[len] = '\0';
                kprintf("\b \b");
            }
            break;

        case '\t':
            complete(line, &len);
            break;

        case 3:                                 /* Ctrl+C : abandonne la ligne */
            kprintf("^C\n");
            line[0] = '\0';
            *len_out = 0;
            return;

        case 12:                                /* Ctrl+L : efface l'écran, garde la ligne */
            vga_clear();
            prompt();
            kprintf("%s", line);
            break;

        case 21:                                /* Ctrl+U : efface la ligne */
            erase_line(len);
            len = 0;
            line[0] = '\0';
            break;

        case KEY_UP: {
            const char *h;

            if (history_at_end(&hist)) {
                memcpy(draft, line, len + 1);   /* garde la saisie en cours */
            }
            h = history_prev(&hist);
            if (h)
                replace_line(line, &len, h);
            break;
        }

        case KEY_DOWN: {
            const char *h = history_next(&hist);

            if (h)
                replace_line(line, &len, h[0] ? h : draft);
            break;
        }

        default:
            if (key >= 32 && key < 256 && key != 127 && len + 1 < LINE_MAX) {
                line[len++] = (char)key;
                line[len] = '\0';
                kprintf("%c", key);
            }
            break;
        }
    }
}

void shell_run(void)
{
    char line[LINE_MAX];
    char copy[LINE_MAX];
    usize len;

    history_init(&hist);

    for (;;) {
        prompt();
        read_line(line, &len);

        history_add(&hist, line);
        memcpy(copy, line, len + 1);            /* execute() découpe la copie sur place */
        execute(copy);
    }
}
