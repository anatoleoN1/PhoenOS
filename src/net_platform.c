/* PhoenOS - net_platform.c : horloge et attente de la pile reseau (noyau). Les tests fournissent les leurs. */
#include "net.h"
#include "pit.h"
#include "sched.h"

u32 net_now_ms(void) { return pit_uptime_ms(); }

void net_delay_ms(u32 ms)
{
    if (sched_ready())
        sched_sleep_ms(ms);
    else {
        u32 t = pit_uptime_ms();

        while (pit_uptime_ms() - t < ms)
            __asm__ volatile ("hlt");
    }
}
