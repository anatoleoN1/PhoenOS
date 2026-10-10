/*
 * PhoenOS - shell.c
 * Shell en ligne de commande : édition de ligne, historique (flèches haut/bas),
 * complétion par Tab, commandes intégrées.
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
#include "io.h"

#define LINE_MAX 256
#define MAX_ARGS 8

typedef err_t (*cmd_fn)(int argc, char **argv);

struct command {
    const char *name;
    const char *usage;      /* arguments, pour l'aide */
    const char *help;
    cmd_fn      fn;
};

static struct history hist;

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

/* ------------------------------------------------------------------ */
/* Table des commandes (ordre = ordre d'affichage de `help`)           */
/* ------------------------------------------------------------------ */

static const struct command commands[] = {
    { "help",     "[commande]",  "Liste les commandes ou detaille l'une d'elles",   cmd_help     },
    { "clear",    "",            "Efface l'ecran (Ctrl+L aussi)",                  cmd_clear    },
    { "version",  "",            "Version et date de compilation",                 cmd_version  },
    { "uptime",   "",            "Temps depuis le demarrage",                      cmd_uptime   },
    { "mem",      "",            "Etat de la memoire physique et du tas",          cmd_mem      },
    { "memtest",  "[Kio]",       "Teste le tas : ecrit et relit des motifs",       cmd_memtest  },
    { "echo",     "[texte...]",  "Affiche son texte (\"entre guillemets\" permis)", cmd_echo     },
    { "sleep",    "<ms>",        "Attend un nombre de millisecondes",              cmd_sleep    },
    { "colors",   "",            "Affiche les 16 couleurs du mode texte",          cmd_colors   },
    { "dmesg",    "",            "Rejoue tout le journal du noyau",                cmd_dmesg    },
    { "history",  "",            "Liste les commandes precedentes",                cmd_history  },
    { "crash",    "<type>",      "Provoque une erreur : voir l'ecran d'erreur",    cmd_crash    },
    { "reboot",   "",            "Redemarre la machine",                           cmd_reboot   },
    { "shutdown", "",            "Eteint la machine (QEMU, VirtualBox)",           cmd_shutdown },
    { "halt",     "",            "Arrete le processeur",                           cmd_halt     },
};

#define COMMAND_COUNT ((int)(sizeof(commands) / sizeof(commands[0])))


/* ------------------------------------------------------------------ */
/* Utilitaires d'affichage                                             */
/* ------------------------------------------------------------------ */

static void color(enum vga_color fg)
{
    vga_set_color(fg, VGA_BLACK);
}

static void pad(const char *s, usize width)
{
    usize n = strlen(s);

    kprintf("%s", s);
    while (n++ < width)
        kprintf(" ");
}

static err_t usage(const char *text)
{
    color(VGA_YELLOW);
    kprintf("usage : %s\n", text);
    color(VGA_LIGHT_GREY);
    return E_REPORTED;                          /* déjà expliqué : execute() n'ajoute rien */
}

/* ------------------------------------------------------------------ */
/* Commandes                                                           */
/* ------------------------------------------------------------------ */

static err_t cmd_help(int argc, char **argv)
{
    const int n = COMMAND_COUNT;

    if (argc >= 2) {
        for (int i = 0; i < n; i++) {
            if (strcmp(commands[i].name, argv[1]) == 0) {
                kprintf("%s %s\n  %s\n", commands[i].name, commands[i].usage, commands[i].help);
                return E_OK;
            }
        }
        kprintf("Pas d'aide pour '%s'.\n", argv[1]);
        return E_REPORTED;
    }

    kprintf("Commandes disponibles (Tab complete, fleches haut/bas = historique) :\n\n");
    for (int i = 0; i < n; i++) {
        color(VGA_LIGHT_CYAN);
        kprintf("  ");
        pad(commands[i].name, 10);
        color(VGA_LIGHT_GREY);
        kprintf("%s\n", commands[i].help);
    }
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
    for (int i = 1; i < argc; i++)
        kprintf("%s%s", i > 1 ? " " : "", argv[i]);
    kprintf("\n");
    return E_OK;
}

static err_t cmd_sleep(int argc, char **argv)
{
    u32 ms;

    if (argc != 2 || cmdline_parse_u32(argv[1], &ms) != E_OK)
        return usage("sleep <millisecondes>");
    if (ms > 60000) {
        kprintf("Maximum : 60000 ms.\n");
        return E_REPORTED;
    }
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
    if (kb == 0 || kb > 4096) {
        kprintf("Taille entre 1 et 4096 Kio.\n");
        return E_REPORTED;
    }

    size = (usize)kb * 1024;
    buf = kmalloc(size);
    if (!buf) {
        kprintf("kmalloc(%u Kio) a echoue : le tas est trop petit ou fragmente.\n", kb);
        return E_NOMEM;
    }

    t0 = pit_uptime_ms();
    for (u32 pass = 0; pass < 4; pass++) {
        static const u8 pattern[4] = { 0x00, 0xFF, 0xAA, 0x55 };

        memset(buf, pattern[pass], size);
        for (usize i = 0; i < size; i++) {
            if (buf[i] != pattern[pass]) {
                kprintf("ERREUR a l'offset %u : lu %x, attendu %x\n", (u32)i, buf[i], pattern[pass]);
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
        kprintf("Type inconnu '%s'. Types : %s\n", argv[1], kinds);
        return E_REPORTED;
    }
    return E_OK;
}

static err_t cmd_reboot(int argc, char **argv)
{
    (void)argc; (void)argv;
    kprintf("Redemarrage...\n");
    power_reboot();
}

static err_t cmd_shutdown(int argc, char **argv)
{
    (void)argc; (void)argv;
    kprintf("Extinction...\n");
    power_shutdown();
}

static err_t cmd_halt(int argc, char **argv)
{
    (void)argc; (void)argv;
    kprintf("Processeur arrete. Redemarrez la machine manuellement.\n");
    power_halt();
}

static void execute(char *line)
{
    char *argv[MAX_ARGS + 1];
    int argc = cmdline_split(line, argv, MAX_ARGS);
    const int n = COMMAND_COUNT;

    if (argc < 0) {
        color(VGA_LIGHT_RED);
        if (argc == E_2BIG)
            kprintf("Trop d'arguments (maximum %d).\n", MAX_ARGS);
        else
            kprintf("Guillemet non ferme.\n");
        color(VGA_LIGHT_GREY);
        return;
    }
    if (argc == 0)
        return;

    for (int i = 0; i < n; i++) {
        if (strcmp(commands[i].name, argv[0]) == 0) {
            err_t e = commands[i].fn(argc, argv);

            if (e != E_OK && e != E_REPORTED) {
                color(VGA_LIGHT_RED);
                kprintf("%s : echec (%s)\n", argv[0], err_str(e));
                color(VGA_LIGHT_GREY);
            }
            return;
        }
    }

    color(VGA_LIGHT_RED);
    kprintf("Commande inconnue : %s", argv[0]);
    color(VGA_LIGHT_GREY);
    kprintf("  (tapez help)\n");
}

/* ------------------------------------------------------------------ */
/* Saisie d'une ligne                                                  */
/* ------------------------------------------------------------------ */

static void prompt(void)
{
    color(VGA_LIGHT_GREEN);
    kprintf("phoenos");
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

/* Complétion du nom de commande avec Tab (uniquement pour le premier mot). */
static void complete(char *line, usize *len)
{
    const int n = COMMAND_COUNT;
    int matches = 0, last = -1;

    for (usize i = 0; i < *len; i++)
        if (line[i] == ' ' || line[i] == '\t')
            return;

    for (int i = 0; i < n; i++) {
        if (strlen(commands[i].name) >= *len && memcmp(commands[i].name, line, *len) == 0) {
            matches++;
            last = i;
        }
    }

    if (matches == 1) {
        const char *rest = commands[last].name + *len;
        usize add = strlen(rest);

        if (*len + add + 1 < LINE_MAX) {
            memcpy(line + *len, rest, add);
            line[*len + add] = ' ';
            line[*len + add + 1] = '\0';
            kprintf("%s ", rest);
            *len += add + 1;
        }
    } else if (matches > 1) {
        kprintf("\n");
        for (int i = 0; i < n; i++)
            if (strlen(commands[i].name) >= *len && memcmp(commands[i].name, line, *len) == 0)
                kprintf("%s  ", commands[i].name);
        kprintf("\n");
        prompt();
        kprintf("%s", line);
    }
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
