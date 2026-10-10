/*
 * PhoenOS - idt.c
 * Table des descripteurs d'interruption (256 entrées).
 * Les 48 premières pointent vers les stubs de isr.S : 0-31 exceptions CPU,
 * 32-47 IRQ matérielles (PIC remappé).
 */
#include "isr.h"
#include "gdt.h"

struct idt_entry {
    u16 offset_low;
    u16 selector;
    u8  zero;
    u8  type_attr;
    u16 offset_high;
} __attribute__((packed));

struct idt_ptr {
    u16 limit;
    u32 base;
} __attribute__((packed));

#define IDT_ENTRIES   256
#define IDT_GATE_INT  0x8E   /* présent, ring 0, interrupt gate 32 bits */
#define IDT_GATE_USER 0xEE   /* present, ring 3 autorise : appels systeme */
#define ISR_STUBS     48
#define SYSCALL_VECTOR 0x80

extern void isr128(void);

extern u32 isr_stub_table[ISR_STUBS];

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr   idt_pointer;

static void idt_set_gate(u8 vector, u32 handler, u8 type_attr)
{
    idt[vector].offset_low  = (u16)(handler & 0xFFFF);
    idt[vector].selector    = GDT_KERNEL_CODE;
    idt[vector].zero        = 0;
    idt[vector].type_attr   = type_attr;
    idt[vector].offset_high = (u16)((handler >> 16) & 0xFFFF);
}

void idt_init(void)
{
    for (int i = 0; i < ISR_STUBS; i++)
        idt_set_gate((u8)i, isr_stub_table[i], IDT_GATE_INT);

    idt_set_gate(SYSCALL_VECTOR, (u32)isr128, IDT_GATE_USER);

    idt_pointer.limit = (u16)(sizeof(idt) - 1);
    idt_pointer.base  = (u32)&idt;

    __asm__ volatile ("lidt %0" : : "m"(idt_pointer));
}
