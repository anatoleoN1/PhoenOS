/*
 * PhoenOS - pit.c
 * Timer 8253/8254 (canal 0, IRQ 0).
 */
#include "pit.h"
#include "isr.h"
#include "io.h"
#include "panic.h"

#define PIT_BASE_HZ  1193182u
#define PIT_CMD      0x43
#define PIT_CH0      0x40

static volatile u32 ticks;
static u32 frequency = 100;

void (*pit_second_hook)(u32 seconds);

static void pit_irq(struct regs *r)
{
    (void)r;
    ticks++;
    if (pit_second_hook && ticks % (frequency * 5) == 0)
        pit_second_hook(ticks / frequency);
    stack_guard_check();            /* détecte un débordement de pile */
}

void pit_init(u32 hz)
{
    u32 divisor;
    err_t e;

    if (hz < 19)       hz = 19;         /* le diviseur tient sur 16 bits */
    if (hz > 1000)     hz = 1000;
    frequency = hz;
    divisor = PIT_BASE_HZ / hz;

    outb(PIT_CMD, 0x36);                /* canal 0, lo/hi, mode 3 */
    outb(PIT_CH0, (u8)(divisor & 0xFF));
    outb(PIT_CH0, (u8)((divisor >> 8) & 0xFF));

    e = irq_install_handler(0, pit_irq);
    if (e != E_OK)
        PANIC("Impossible d'installer le handler du timer : %s", err_str(e));
}

u32 pit_hz(void)
{
    return frequency;
}

u32 pit_ticks(void)
{
    return ticks;
}

u32 pit_uptime_ms(void)
{
    u32 t = ticks;
    return (t / frequency) * 1000 + ((t % frequency) * 1000) / frequency;
}

void pit_sleep_ms(u32 ms)
{
    u32 target = ticks + (ms * frequency) / 1000;

    while ((i32)(ticks - target) < 0)
        cpu_hlt();
}
