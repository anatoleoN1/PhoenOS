/*
 * PhoenOS - pit.c
 * Timer 8253/8254 (canal 0, IRQ 0).
 */
#include "pit.h"
#include "isr.h"
#include "io.h"

#define PIT_BASE_HZ  1193182u
#define PIT_CMD      0x43
#define PIT_CH0      0x40

static volatile u32 ticks;
static u32 frequency = 100;

static void pit_irq(struct regs *r)
{
    (void)r;
    ticks++;
}

void pit_init(u32 hz)
{
    u32 divisor;

    if (hz < 19)       hz = 19;         /* le diviseur tient sur 16 bits */
    if (hz > 1000)     hz = 1000;
    frequency = hz;
    divisor = PIT_BASE_HZ / hz;

    outb(PIT_CMD, 0x36);                /* canal 0, lo/hi, mode 3 */
    outb(PIT_CH0, (u8)(divisor & 0xFF));
    outb(PIT_CH0, (u8)((divisor >> 8) & 0xFF));

    irq_install_handler(0, pit_irq);
}

u32 pit_ticks(void)
{
    return ticks;
}

void pit_sleep_ms(u32 ms)
{
    u32 target = ticks + (ms * frequency) / 1000;

    while ((i32)(ticks - target) < 0)
        cpu_hlt();
}
