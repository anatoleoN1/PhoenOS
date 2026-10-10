/*
 * PhoenOS - log.c
 * Journal du noyau avec niveau et horodatage (voir aussi dlog.c).
 */
#include <stdarg.h>
#include "log.h"
#include "dlog.h"
#include "kprintf.h"
#include "vga.h"
#include "pit.h"

void klog(enum log_level level, const char *fmt, ...)
{
    static const char *const tag[] = { "TRACE", "DEBUG", "INFO ", "WARN ", "ERROR" };
    static const enum vga_color color[] = {
        VGA_DARK_GREY, VGA_DARK_GREY, VGA_LIGHT_CYAN, VGA_YELLOW, VGA_LIGHT_RED
    };
    char msg[DLOG_MSG_MAX];
    u32 ms = pit_uptime_ms();
    va_list ap;

    if ((int)level < 0 || level > LOG_ERROR)
        level = LOG_ERROR;

    va_start(ap, fmt);
    kvsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    dlog((int)level, "kernel", "%s", msg);

    if (level < LOG_INFO)
        return;                                 /* TRACE / DEBUG : journal seulement, pas d'ecran */
    vga_set_color(color[level], VGA_BLACK);
    keprintf("[%u.%u%u%u %s] ", ms / 1000, (ms / 100) % 10, (ms / 10) % 10, ms % 10,
            tag[level]);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    keprintf("%s\n", msg);
}
