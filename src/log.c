/*
 * PhoenOS - log.c
 * Journal du noyau avec niveau et horodatage.
 */
#include <stdarg.h>
#include "log.h"
#include "kprintf.h"
#include "vga.h"
#include "pit.h"

void klog(enum log_level level, const char *fmt, ...)
{
    static const char *const tag[] = { "DEBUG", "INFO ", "WARN ", "ERROR" };
    static const enum vga_color color[] = {
        VGA_DARK_GREY, VGA_LIGHT_CYAN, VGA_YELLOW, VGA_LIGHT_RED
    };
    u32 ms = pit_uptime_ms();
    va_list ap;

    if ((int)level < 0 || level > LOG_ERROR)
        level = LOG_ERROR;

    vga_set_color(color[level], VGA_BLACK);
    keprintf("[%u.%u%u%u %s] ", ms / 1000, (ms / 100) % 10, (ms / 10) % 10, ms % 10,
            tag[level]);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    va_start(ap, fmt);
    kveprintf(fmt, ap);
    va_end(ap);
    keprintf("\n");
}
