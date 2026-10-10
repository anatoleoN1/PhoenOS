/*
 * PhoenOS - pic.c
 * Contrôleur d'interruptions 8259 (maître + esclave).
 */
#include "pic.h"
#include "io.h"

#define PIC1_CMD   0x20
#define PIC1_DATA  0x21
#define PIC2_CMD   0xA0
#define PIC2_DATA  0xA1

#define PIC_EOI        0x20
#define PIC_READ_ISR   0x0B

void pic_init(void)
{
    /* ICW1 : initialisation en cascade */
    outb(PIC1_CMD, 0x11); io_wait();
    outb(PIC2_CMD, 0x11); io_wait();
    /* ICW2 : décalage des vecteurs (maître 0x20, esclave 0x28) */
    outb(PIC1_DATA, 0x20); io_wait();
    outb(PIC2_DATA, 0x28); io_wait();
    /* ICW3 : l'esclave est branché sur l'IRQ 2 du maître */
    outb(PIC1_DATA, 0x04); io_wait();
    outb(PIC2_DATA, 0x02); io_wait();
    /* ICW4 : mode 8086 */
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();

    /* Tout est masqué, sauf la cascade (IRQ 2) */
    outb(PIC1_DATA, 0xFB);
    outb(PIC2_DATA, 0xFF);
}

void pic_mask(u8 irq)
{
    u16 port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    outb(port, (u8)(inb(port) | (1u << (irq & 7))));
}

void pic_unmask(u8 irq)
{
    u16 port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    outb(port, (u8)(inb(port) & ~(1u << (irq & 7))));
}

void pic_eoi(u8 irq)
{
    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

int pic_is_spurious(u8 irq)
{
    u16 cmd;

    if (irq != 7 && irq != 15)
        return 0;

    cmd = (irq == 7) ? PIC1_CMD : PIC2_CMD;
    outb(cmd, PIC_READ_ISR);
    if (inb(cmd) & 0x80)
        return 0;                 /* vraie interruption */

    /* Fausse IRQ 15 : le maître a quand même reçu l'IRQ 2 de la cascade */
    if (irq == 15)
        outb(PIC1_CMD, PIC_EOI);
    return 1;
}
