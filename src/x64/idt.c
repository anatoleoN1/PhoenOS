/*
 * PhoenOS perf64 - x64/idt.c
 * IDT du mode long (portes de 16 octets). 0-31 exceptions, 32-47 IRQ (PIC remappe), 0x80 appels systeme (ring 3 autorise).
 * La double faute (8) utilise la pile dediee IST1 du TSS.
 */
#include "isr.h"
#include "gdt.h"

struct idt_entry {
    u16 offset_low;
    u16 selector;
    u8  ist;
    u8  type_attr;
    u16 offset_mid;
    u32 offset_high;
    u32 zero;
} __attribute__((packed));

struct idt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

#define IDT_ENTRIES   256
#define IDT_GATE_INT  0x8E   /* present, ring 0, porte d'interruption 64 bits */
#define IDT_GATE_USER 0xEE   /* present, ring 3 autorise : appels systeme */
#define ISR_STUBS     48
#define SYSCALL_VECTOR 0x80

extern void isr128(void);
extern usize isr_stub_table[ISR_STUBS];

static struct idt_entry idt[IDT_ENTRIES] __attribute__((aligned(16)));
static struct idt_ptr   idt_pointer;

static void idt_set_gate(u8 vector, usize handler, u8 type_attr, u8 ist)
{
    idt[vector].offset_low  = (u16)(handler & 0xFFFF);
    idt[vector].selector    = GDT_KERNEL_CODE;
    idt[vector].ist         = ist;
    idt[vector].type_attr   = type_attr;
    idt[vector].offset_mid  = (u16)((handler >> 16) & 0xFFFF);
    idt[vector].offset_high = (u32)(handler >> 32);
    idt[vector].zero        = 0;
}

void idt_init(void)
{
    for (int i = 0; i < ISR_STUBS; i++)
        idt_set_gate((u8)i, isr_stub_table[i], IDT_GATE_INT, i == 8 ? 1 : 0);

    idt_set_gate(SYSCALL_VECTOR, (usize)isr128, IDT_GATE_USER, 0);

    idt_pointer.limit = (u16)(sizeof(idt) - 1);
    idt_pointer.base  = (u64)(usize)idt;

    __asm__ volatile ("lidt %0" : : "m"(idt_pointer));
}
