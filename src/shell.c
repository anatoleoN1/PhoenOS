/*
 * PhoenOS - shell.c
 * Shell en ligne de commande : édition de ligne, historique (flèches haut/bas),
 * complétion par Tab (commandes et chemins), redirection > et >>, commandes intégrées.
 *
 * Ajouter une commande : écrire `static err_t cmd_xxx(int argc, char **argv)`
 * puis l'inscrire dans la table `commands` plus bas.
 */
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
#include "io.h"

#define LINE_MAX 256
#define MAX_ARGS 8
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

/* Prototypes des commandes (la table qui les référence est juste en dessous). */
static err_t cmd_help(int argc, char **argv);
static err_t cmd_clear(int argc, char **argv);
static err_t cmd_version(int argc, char **argv);
static err_t cmd_uptime(int argc, char **argv);
static err_t cmd_mem(int argc, char **argv);
static err_t cmd_memtest(int argc, char **argv);
static err_t cmd_echo(int argc, char **argv);
static err_t cmd_sleep(int argc, char **argv);
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

/* ------------------------------------------------------------------ */
/* Table des commandes (ordre = ordre d'affichage de `help`)           */
/* ------------------------------------------------------------------ */

#define G_SYS "Systeme"
#define G_FS  "Fichiers"
#define G_TXT "Texte"

static const struct command commands[] = {
    { G_SYS, "help",     "[commande]",   "Liste les commandes, ou detaille l'une d'elles",  cmd_help     },
    { G_SYS, "clear",    "",             "Efface l'ecran (Ctrl+L aussi)",                  cmd_clear    },
    { G_SYS, "version",  "",             "Version et date de compilation",                 cmd_version  },
    { G_SYS, "uptime",   "",             "Temps depuis le demarrage",                      cmd_uptime   },
    { G_SYS, "mem",      "",             "Etat de la memoire physique et du tas",          cmd_mem      },
    { G_SYS, "memtest",  "[Kio]",        "Teste le tas : ecrit et relit des motifs",       cmd_memtest  },
    { G_SYS, "sleep",    "<ms>",         "Attend un nombre de millisecondes",              cmd_sleep    },
    { G_SYS, "colors",   "",             "Affiche les 16 couleurs du mode texte",          cmd_colors   },
    { G_SYS, "dmesg",    "",             "Rejoue tout le journal du noyau",                cmd_dmesg    },
    { G_SYS, "history",  "",             "Liste les commandes precedentes",                cmd_history  },
    { G_SYS, "crash",    "<type>",       "Provoque une erreur : voir l'ecran d'erreur",    cmd_crash    },
    { G_SYS, "reboot",   "",             "Redemarre la machine",                           cmd_reboot   },
    { G_SYS, "shutdown", "",             "Eteint la machine (QEMU, VirtualBox)",           cmd_shutdown },
    { G_SYS, "halt",     "",             "Arrete le processeur",                           cmd_halt     },
    { G_FS,  "ls",       "[-l] [chemin]", "Liste un dossier (-l : type et taille)",        cmd_ls       },
    { G_FS,  "cat",      "<fichier>...", "Affiche le contenu de fichiers",                 cmd_cat      },
    { G_FS,  "cd",       "[dossier]",    "Change de dossier (sans argument : /)",          cmd_cd       },
    { G_FS,  "pwd",      "",             "Affiche le dossier courant",                     cmd_pwd      },
    { G_FS,  "mkdir",    "[-p] <dossier>...", "Cree des dossiers (-p : avec les parents)", cmd_mkdir    },
    { G_FS,  "touch",    "<fichier>...", "Cree des fichiers vides",                        cmd_touch    },
    { G_FS,  "rm",       "[-r] <chemin>...", "Supprime (-r : dossiers et contenu)",        cmd_rm       },
    { G_FS,  "rmdir",    "<dossier>...", "Supprime des dossiers vides",                    cmd_rmdir    },
    { G_FS,  "cp",       "<source> <destination>", "Copie un fichier",                     cmd_cp       },
    { G_FS,  "mv",       "<source> <destination>", "Deplace ou renomme",                   cmd_mv       },
    { G_FS,  "tree",     "[dossier]",    "Affiche l'arborescence",                         cmd_tree     },
    { G_FS,  "df",       "",             "Occupation du systeme de fichiers",              cmd_df       },
    { G_FS,  "hexdump",  "<fichier> [octets]", "Affiche un fichier en hexadecimal",        cmd_hexdump  },
    { G_TXT, "echo",     "[-n] [texte...]", "Affiche son texte (\"entre guillemets\" permis)", cmd_echo },
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
    static const char *const groups[] = { G_SYS, G_FS, G_TXT };

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
    kprintf("Redirection : commande > fichier   ou   commande >> fichier (ajoute)\n");
    kprintf("Tapez help <commande> pour le detail.\n");
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
    pit_sleep_ms(ms);
    return E_OK;
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
    static const char *const kinds = "div0 | gp | ud | panic | assert | doublefree | heap";

    if (argc != 2)
        return usage("crash <div0|gp|ud|panic|assert|doublefree|heap>");

    if (strcmp(argv[1], "div0") == 0) {
        volatile int zero = 0;
        volatile int r = 1 / zero;
        (void)r;
    } else if (strcmp(argv[1], "gp") == 0) {
        __asm__ volatile ("movw $0x99, %%ax\n\tmovw %%ax, %%ds" : : : "eax");
    } else if (strcmp(argv[1], "ud") == 0) {
        __asm__ volatile ("ud2");
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

static err_t cmd_reboot(int argc, char **argv)
{
    (void)argc; (void)argv;
    kprintf_redirect(NULL, NULL);
    kprintf("Redemarrage...\n");
    power_reboot();
}

static err_t cmd_shutdown(int argc, char **argv)
{
    (void)argc; (void)argv;
    kprintf_redirect(NULL, NULL);
    kprintf("Extinction...\n");
    power_shutdown();
}

static err_t cmd_halt(int argc, char **argv)
{
    (void)argc; (void)argv;
    kprintf_redirect(NULL, NULL);
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

static err_t cmd_cat(int argc, char **argv)
{
    err_t result = E_OK;

    if (argc < 2)
        return usage("cat <fichier>...");

    for (int i = 1; i < argc; i++) {
        struct fs_node *n;
        err_t e = fs_lookup(argv[i], &n);

        if (e == E_OK && n->type != FS_FILE)
            e = E_ISDIR;
        if (e != E_OK) {
            result = fail_path("cat", argv[i], e);
            continue;
        }
        for (u32 k = 0; k < n->size; k++) {
            u8 c = n->data[k];

            /* À l'écran, les octets de contrôle deviennent '.' ; redirigé, copie fidèle. */
            if (!raw_output && c != '\n' && c != '\t' && (c < 32 || c == 127))
                c = '.';
            kprintf("%c", c);
        }
        if (!raw_output && n->size && n->data[n->size - 1] != '\n')
            kprintf("\n");
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

static void tree_print(const struct fs_node *dir, int level, u32 *dirs, u32 *files)
{
    for (const struct fs_node *c = dir->child; c; c = c->next) {
        for (int i = 0; i < level; i++)
            kprintf("    ");
        kprintf("+-- ");
        print_entry_name(c);
        kprintf("\n");
        if (c->type == FS_DIR) {
            (*dirs)++;
            tree_print(c, level + 1, dirs, files);
        } else {
            (*files)++;
        }
    }
}

static err_t cmd_tree(int argc, char **argv)
{
    struct fs_node *n;
    const char *target = argc >= 2 ? argv[1] : ".";
    u32 dirs = 0, files = 0;
    err_t e;

    if (argc > 2)
        return usage("tree [dossier]");
    e = fs_lookup(target, &n);
    if (e == E_OK && n->type != FS_DIR)
        e = E_NOTDIR;
    if (e != E_OK)
        return fail_path("tree", target, e);

    color(VGA_LIGHT_CYAN);
    kprintf("%s\n", n == fs_root() ? "/" : target);
    color(VGA_LIGHT_GREY);
    tree_print(n, 0, &dirs, &files);
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

static err_t run_command(int argc, char **argv)
{
    for (int i = 0; i < COMMAND_COUNT; i++) {
        if (strcmp(commands[i].name, argv[0]) == 0) {
            err_t e = commands[i].fn(argc, argv);

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

static void execute(char *line)
{
    char *argv[MAX_ARGS + 1];
    u8 quoted[MAX_ARGS + 1];
    int argc = cmdline_split_ex(line, argv, MAX_ARGS, quoted);
    const char *target = NULL;
    int append = 0;
    struct fs_node *node = NULL;
    struct capture cap;
    err_t e;

    if (argc < 0) {
        fail_msg(argc == E_2BIG ? "Trop d'arguments (maximum 8)." : "Guillemet non ferme.");
        return;
    }
    if (argc == 0)
        return;

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

    if (!target) {
        run_command(argc, argv);
        color(VGA_LIGHT_GREY);
        return;
    }

    /* Le fichier est créé AVANT la commande : erreur claire si le chemin est faux. */
    e = fs_create(target, &node);
    if (e != E_OK) {
        fail_path("redirection", target, e);
        return;
    }

    memset(&cap, 0, sizeof(cap));
    raw_output = 1;
    kprintf_redirect(capture_sink, &cap);
    run_command(argc, argv);
    kprintf_redirect(NULL, NULL);
    raw_output = 0;
    color(VGA_LIGHT_GREY);

    if (cap.nomem) {
        fail_msg("Memoire insuffisante : sortie perdue.");
    } else {
        e = append ? fs_append(node, cap.buf, cap.len) : fs_write(node, cap.buf, cap.len);
        if (e != E_OK)
            fail_path("redirection", target, e);
        else if (cap.overflow)
            fail_msg("Sortie tronquee a 128 Kio.");
    }
    kfree(cap.buf);
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
