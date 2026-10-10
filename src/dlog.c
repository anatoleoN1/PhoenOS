/* PhoenOS - dlog.c : journal de debogage (voir dlog.h). */
#include <stdarg.h>
#include "dlog.h"
#include "kprintf.h"
#include "io.h"
#include "pit.h"
#include "string.h"

#define DEBUG_PORT 0xE9

struct entry {
    u32  ms;
    u8   level;
    char tag[DLOG_TAG_MAX];
    char msg[DLOG_MSG_MAX];
};

static struct entry ring[DLOG_ENTRIES];
static u32 total;                       /* numero de la prochaine ligne */
static int threshold = DL_DEBUG;
static int port_ok;
static int in_dlog;                     /* garde : un dlog dans un dlog est ignore */

static const char *const names[] = { "TRACE", "DEBUG", "INFO ", "WARN ", "ERROR" };

void dlog_init(void)
{
    port_ok = (inb(DEBUG_PORT) == 0xE9);        /* QEMU (-debugcon) et Bochs repondent 0xE9 sur ce port */
    threshold = port_ok ? DL_TRACE : DL_DEBUG;
}

int dlog_port_present(void)
{
    return port_ok;
}

void dlog_panic_mode(void)
{
    in_dlog = 0;
    threshold = DL_TRACE;
}

int dlog_level(void)
{
    return threshold;
}

void dlog_set_level(int level)
{
    if (level < DL_TRACE)
        level = DL_TRACE;
    if (level > DL_ERROR)
        level = DL_ERROR;
    threshold = level;
}

const char *dlog_level_name(int level)
{
    return (level >= DL_TRACE && level <= DL_ERROR) ? names[level] : "?????";
}

int dlog_parse_level(const char *s)
{
    static const char *const low[] = { "trace", "debug", "info", "warn", "error" };

    if (!s || !*s)
        return -1;
    if (s[1] == '\0' && s[0] >= '0' && s[0] <= '4')
        return s[0] - '0';
    for (int i = 0; i < 5; i++) {
        const char *a = s, *b = low[i];

        while (*a && *b && (*a | 0x20) == *b) {
            a++;
            b++;
        }
        if (!*a && !*b)
            return i;
    }
    return -1;
}

usize dlog_format(char *buf, usize size, u32 ms, int level, const char *tag, const char *msg)
{
    return ksnprintf(buf, size, "[%4u.%03u %s %-8s] %s", ms / 1000, ms % 1000, dlog_level_name(level), tag, msg);
}

static void port_write(const char *s, usize n)
{
    for (usize i = 0; i < n; i++)
        outb(DEBUG_PORT, (u8)s[i]);
}

void dlog(int level, const char *tag, const char *fmt, ...)
{
    char line[DLOG_MSG_MAX + 40];
    char msg[DLOG_MSG_MAX];
    va_list ap;
    u32 ms, flags, slot;
    usize n;

    if (level < threshold || in_dlog)
        return;
    if (level > DL_ERROR)
        level = DL_ERROR;
    in_dlog = 1;
    ms = pit_uptime_ms();

    va_start(ap, fmt);
    kvsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    for (usize i = 0; msg[i]; i++)                  /* une ligne = une ligne : pas de retour a la ligne dans le message */
        if (msg[i] == '\n' || msg[i] == '\r')
            msg[i] = ' ';

    flags = cpu_irq_save();
    slot = total++ % DLOG_ENTRIES;
    ring[slot].ms = ms;
    ring[slot].level = (u8)level;
    for (usize i = 0; i < DLOG_TAG_MAX; i++)
        ring[slot].tag[i] = 0;
    for (usize i = 0; i + 1 < DLOG_TAG_MAX && tag && tag[i]; i++)
        ring[slot].tag[i] = tag[i];
    memcpy(ring[slot].msg, msg, sizeof msg);
    cpu_irq_restore(flags);

    if (port_ok) {
        n = dlog_format(line, sizeof line - 1, ms, level, ring[slot].tag, msg);
        if (n > sizeof line - 2)
            n = sizeof line - 2;
        line[n++] = '\n';
        port_write(line, n);
    }
    in_dlog = 0;
}

u32 dlog_total(void)
{
    return total;
}

u32 dlog_kept(void)
{
    return total < DLOG_ENTRIES ? total : DLOG_ENTRIES;
}

void dlog_clear(void)
{
    u32 f = cpu_irq_save();

    total = 0;
    cpu_irq_restore(f);
}

int dlog_get(u32 n, u32 *ms, int *level, char *tag, char *msg)
{
    u32 kept = dlog_kept(), first, slot;

    if (n >= kept)
        return 0;
    first = total - kept;
    slot = (first + n) % DLOG_ENTRIES;
    *ms = ring[slot].ms;
    *level = ring[slot].level;
    memcpy(tag, ring[slot].tag, DLOG_TAG_MAX);
    memcpy(msg, ring[slot].msg, DLOG_MSG_MAX);
    return 1;
}
