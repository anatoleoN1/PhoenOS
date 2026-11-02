#ifndef PHOENOS_ISR_H
#define PHOENOS_ISR_H

#include "types.h"
#include "error.h"

/*
 * État du processeur sauvegardé à l'entrée d'une interruption.
 * L'ordre correspond à la pile construite dans isr_stubs.S (noyau en ring 0 :
 * le CPU ne pousse ni ESP ni SS).
 */
#ifdef __x86_64__
/* perf64 : x64/isr_stubs.S pousse les 15 registres a la main ; le CPU pousse toujours rsp et ss. */
struct regs {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    union { u64 rdi; u32 edi; };                       /* les alias e.. servent aux programmes 32 bits (mode de compatibilite) */
    union { u64 rsi; u32 esi; };
    union { u64 rbp; u32 ebp; };
    union { u64 rbx; u32 ebx; };
    union { u64 rdx; u32 edx; };
    union { u64 rcx; u32 ecx; };
    union { u64 rax; u32 eax; };
    u64 int_no, err_code;                              /* stubs */
    union { u64 rip; u32 eip; };                       /* CPU   */
    u64 cs;
    union { u64 rflags; u32 eflags; };
    union { u64 rsp; u32 useresp; };
    u64 ss;
};
#else
struct regs {
    u32 edi, esi, ebp, esp_pusha, ebx, edx, ecx, eax;  /* pusha */
    u32 int_no, err_code;                              /* stubs */
    u32 eip, cs, eflags;                               /* CPU   */
    u32 useresp, ss;                                   /* CPU, seulement si (cs & 3) == 3 */
};
#endif

typedef void (*irq_handler_t)(struct regs *r);

#define IRQ_BASE 32

void idt_init(void);

/*
 * Installe un handler pour l'IRQ n (0-15) et la démasque.
 * E_INVAL : IRQ hors limites ou handler nul ; E_BUSY : IRQ déjà prise.
 */
err_t irq_install_handler(u8 irq, irq_handler_t handler);

/* Nombre de breakpoints (int3) traités : sert d'auto-test de l'IDT. */
u32 isr_breakpoint_count(void);

#endif
