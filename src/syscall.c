/*
 * PhoenOS - syscall.c
 * Appels systeme (int 0x80). Convention : numero dans EAX, arguments dans EBX, ECX, EDX,
 * resultat dans EAX (negatif = code d'erreur E_*). Les numeros communs sont ceux de Linux i386.
 * Tout pointeur recu d'un programme est verifie : il doit rester dans la zone du programme.
 */
#include "user.h"
#include "aspace.h"
#include "fs.h"
#include "kprintf.h"
#include "keyboard.h"
#include "pit.h"
#include "sched.h"
#include "rtc.h"
#include "string.h"

#define WRITE_MAX    (1u << 20)         /* octets ecrits par appel au plus */
#define SLEEP_MAX_MS 60000u
#define PATH_MAX_LEN 256
#define CTRL_C       3
#define CTRL_D       4

/* Options d'open (memes valeurs que Linux i386) */
#define O_ACCMODE 3
#define O_WRONLY  1
#define O_RDWR    2
#define O_CREAT   0x40
#define O_TRUNC   0x200
#define O_APPEND  0x400

static u32 next_pid = 1;

void user_proc_reset(struct uproc *p, const char *name)
{
    memset(p, 0, sizeof(*p));
    p->win_lo = USER_BASE;
    p->win_hi = USER_END;
    p->pid = next_pid++;
    for (usize i = 0; i + 1 < sizeof(p->name) && name[i]; i++)
        p->name[i] = name[i];
}

/* [addr, addr+len) est-il dans la fenetre du programme ET (s'il a un espace d'adressage) fait-il
 * partie de ses pages mappees ? write = 1 : le noyau va y ECRIRE (la page doit etre modifiable). */
static int user_range(const struct uproc *p, u32 addr, u32 len, int write)
{
    u32 end = addr + len;

    if (addr < p->win_lo || addr > p->win_hi || end < addr || end > p->win_hi)
        return 0;
    return !p->as || aspace_check(p->as, addr, len, write);
}

/* Copie une chaine terminee par 0 depuis le programme ; -1 si elle sort de la zone ou est trop longue. */
static int user_string(const struct uproc *p, u32 addr, char *dst, u32 max)
{
    for (u32 i = 0; i < max; i++) {
        if (!user_range(p, addr + i, 1, 0))
            return -1;
        dst[i] = *(const char *)(addr + i);
        if (!dst[i])
            return 0;
    }
    return -1;
}

static void out_bytes(const u8 *d, u32 n, int to_error)
{
    for (u32 i = 0; i < n; i++) {
        if (to_error)
            keprintf("%c", d[i]);
        else
            kprintf("%c", d[i]);
    }
}

/* Lit une ligne au clavier (avec echo et retour arriere). 0 : Ctrl+D sur ligne vide ; -1 : Ctrl+C. */
static int read_console_line(struct uproc *p)
{
    p->line_len = p->line_pos = 0;
    for (;;) {
        int c = keyboard_getc();

        if (c == CTRL_C) {
            kprintf("^C\n");
            return -1;
        }
        if (c == CTRL_D) {
            if (p->line_len == 0)
                return 0;
            continue;
        }
        if (c == '\n' || c == '\r') {
            kprintf("\n");
            p->line[p->line_len++] = '\n';
            return 1;
        }
        if (c == '\b') {
            if (p->line_len) {
                p->line_len--;
                kprintf("\b");
            }
            continue;
        }
        if (c >= 32 && c < 256 && p->line_len < sizeof(p->line) - 1) {
            p->line[p->line_len++] = (char)c;
            kprintf("%c", (char)c);
        }
    }
}

/* Descripteur de fichier valide ? Si le fichier a ete supprime entre-temps (par un autre programme ou le
 * shell), le descripteur est ferme et on renvoie NULL. */
static struct user_fd *get_fd(struct uproc *p, u32 fd)
{
    struct user_fd *f;

    if (fd < USER_FIRST_FD || fd >= USER_FIRST_FD + USER_MAX_FDS)
        return NULL;
    f = &p->fd[fd - USER_FIRST_FD];
    if (!f->node)
        return NULL;
    if (!fs_is_live(f->node)) {
        f->node = NULL;
        return NULL;
    }
    return f;
}

static i32 sys_read(struct uproc *p, u32 fd, u32 buf, u32 n)
{
    u32 avail, take;

    if (!user_range(p, buf, n, 1))
        return E_FAULT;
    if (n == 0)
        return 0;

    if (fd == 0) {
        if (p->in_set) {
            avail = p->in_len - p->in_pos;
            take = n < avail ? n : avail;
            memcpy((void *)buf, p->in + p->in_pos, take);
            p->in_pos += take;
            return (i32)take;
        }
        if (p->line_pos >= p->line_len) {
            int r = read_console_line(p);

            if (r < 0) {
                p->exit_code = 130;
                p->exit_requested = 1;
                return 0;
            }
            if (r == 0)
                return 0;                                       /* fin de fichier */
        }
        avail = p->line_len - p->line_pos;
        take = n < avail ? n : avail;
        memcpy((void *)buf, p->line + p->line_pos, take);
        p->line_pos += take;
        return (i32)take;
    }
    {
        struct user_fd *f = get_fd(p, fd);

        if (!f)
            return E_INVAL;
        if (!f->readable)
            return E_INVAL;
        avail = f->pos < f->node->size ? f->node->size - f->pos : 0;
        take = n < avail ? n : avail;
        memcpy((void *)buf, f->node->data + f->pos, take);
        f->pos += take;
        return (i32)take;
    }
}

static i32 sys_write(struct uproc *p, u32 fd, u32 buf, u32 n)
{
    if (n > WRITE_MAX)
        n = WRITE_MAX;
    if (fd == 1 || fd == 2) {
        if (!user_range(p, buf, n, 0))
            return E_FAULT;
        out_bytes((const u8 *)buf, n, fd == 2);
        return (i32)n;
    }
    {
        struct user_fd *f = get_fd(p, fd);
        err_t e;

        if (!f)
            return E_INVAL;

        if (!f->writable)
            return E_INVAL;
        if (!user_range(p, buf, n, 0))
            return E_FAULT;
        e = fs_append(f->node, (const void *)buf, n);           /* toujours en fin de fichier */
        if (e != E_OK)
            return e;
        f->pos = f->node->size;
        return (i32)n;
    }
}

static i32 sys_open(struct uproc *p, u32 path_addr, u32 flags)
{
    char path[PATH_MAX_LEN];
    struct fs_node *node;
    int slot = -1, write = (int)(flags & O_ACCMODE);
    err_t e;

    if (flags & ~(u32)(O_ACCMODE | O_CREAT | O_TRUNC | O_APPEND) || write == 3)
        return E_INVAL;
    if ((flags & (O_CREAT | O_TRUNC)) && !write)
        return E_INVAL;                                         /* creer / vider demande l'ecriture */
    if (user_string(p, path_addr, path, sizeof path) != 0)
        return E_FAULT;
    for (int i = 0; i < USER_MAX_FDS; i++) {
        if (!p->fd[i].node) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return E_NOMEM;
    e = fs_lookup(path, &node);
    if (e == E_NOENT && (flags & O_CREAT))
        e = fs_create(path, &node);
    if (e != E_OK)
        return e;
    if (node->type != FS_FILE)
        return E_ISDIR;
    if ((flags & O_TRUNC) && (e = fs_write(node, NULL, 0)) != E_OK)
        return e;
    p->fd[slot].node = node;
    p->fd[slot].pos = (flags & O_APPEND) ? node->size : 0;
    p->fd[slot].writable = write != 0;
    p->fd[slot].readable = write != O_WRONLY;
    return USER_FIRST_FD + slot;
}

/* Supprime un fichier (pas un dossier) ; les descripteurs ouverts dessus sont fermes. */
static i32 sys_unlink(struct uproc *p, u32 path_addr)
{
    char path[PATH_MAX_LEN];
    struct fs_node *node;
    err_t e;

    if (user_string(p, path_addr, path, sizeof path) != 0)
        return E_FAULT;
    e = fs_lookup(path, &node);
    if (e != E_OK)
        return e;
    if (node->type != FS_FILE)
        return E_ISDIR;
    for (int i = 0; i < USER_MAX_FDS; i++) {
        if (p->fd[i].node == node)
            p->fd[i].node = NULL;
    }
    return fs_remove(path, 0);
}

static i32 sys_close(struct uproc *p, u32 fd)
{
    if (fd >= USER_FIRST_FD && fd < USER_FIRST_FD + USER_MAX_FDS && p->fd[fd - USER_FIRST_FD].node) {
        p->fd[fd - USER_FIRST_FD].node = NULL;
        return 0;
    }
    return E_INVAL;
}

i32 syscall_dispatch(struct uproc *p, u32 nr, u32 a, u32 b, u32 c)
{
    switch (nr) {
    case SYS_EXIT:
        p->exit_code = (int)(a & 0xFF);
        p->exit_requested = 1;
        return 0;
    case SYS_READ:
        return sys_read(p, a, b, c);
    case SYS_WRITE:
        return sys_write(p, a, b, c);
    case SYS_OPEN:
        return sys_open(p, a, b);
    case SYS_CLOSE:
        return sys_close(p, a);
    case SYS_UNLINK:
        return sys_unlink(p, a);
    case SYS_TIME: {
        u32 t = rtc_now_unix();

        if (a) {
            if (!user_range(p, a, 4, 1))
                return E_FAULT;
            memcpy((void *)a, &t, 4);
        }
        return (i32)t;
    }
    case SYS_GETPID:
        return (i32)p->pid;
    case SYS_SLEEP_MS:
        if (sched_ready())
            sched_sleep_ms(a > SLEEP_MAX_MS ? SLEEP_MAX_MS : a);      /* les autres programmes tournent pendant ce temps */
        else
            pit_sleep_ms(a > SLEEP_MAX_MS ? SLEEP_MAX_MS : a);
        return 0;
    case SYS_UPTIME:
        return (i32)pit_uptime_ms();
    default:
        return E_NOSYS;
    }
}
