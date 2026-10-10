#ifndef PHOENOS_PIC_H
#define PHOENOS_PIC_H

#include "types.h"

void pic_init(void);              /* remappe les IRQ sur les vecteurs 32-47 */
void pic_mask(u8 irq);
void pic_unmask(u8 irq);
void pic_eoi(u8 irq);             /* fin d'interruption */
int  pic_is_spurious(u8 irq);     /* vrai pour une fausse IRQ 7 / 15 */

#endif
