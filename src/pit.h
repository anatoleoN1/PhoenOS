#ifndef PHOENOS_PIT_H
#define PHOENOS_PIT_H

#include "types.h"

void pit_init(u32 hz);            /* démarre le timer (IRQ 0) à `hz` ticks/s */
u32  pit_ticks(void);             /* ticks depuis le démarrage */
void pit_sleep_ms(u32 ms);        /* attente passive (interruptions actives) */

#endif
