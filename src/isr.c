/*
 * PhoenOS - isr.c
 * Dispatch des interruptions : exceptions CPU (écran de panique) et IRQ.
 */
#include "isr.h"
#include "pic.h"
#include "io.h"
#include "vga.h"
#include "kprintf.h"

static const char *const exception_names[32] = {
    "Division par zero",              /*  0 #DE */
    "Debug",                          /*  1 #DB */
    "Interruption non masquable",     /*  2 NMI */
    "Point d'arret",                  /*  3 #BP */
    "Debordement",                    /*  4 #OF */
    "Depassement de bornes",          /*  5 #BR */
    "Instruction invalide",           /*  6 #UD */
    "Coprocesseur indisponible",      /*  7 #NM */
    "Double faute",                   /*  8 #DF */
    "Coprocesseur (obsolete)",        /*  9     */
    "TSS invalide",                   /* 10 #TS */
    "Segment absent",                 /* 11 #NP */
    "Faute de pile",                  /* 12 #SS */
    "Protection generale",            /* 13 #GP */
    "Faute de page",                  /* 14 #PF */
    "Reserve",                        /* 15     */
    "Erreur x87",                     /* 16 #MF */
    "Alignement",                     /* 17 #AC */
    "Erreur machine",                 /* 18 #MC */
    "Erreur SIMD",                    /* 19 #XM */
    "Virtualisation",                 /* 20 #VE */
    "Protection de flux",             /* 21 #CP */
    "Reserve", "Reserve", "Reserve", "Reserve", "Reserve", "Reserve",
    "Injection hyperviseur",          /* 28 */
    "Communication VMM",              /* 29 */
    "Securite",                       /* 30 */
    "Reserve"                         /* 31 */
};

static irq_handler_t irq_handlers[16];
static volatile u32 breakpoint_hits;

void irq_install_handler(u8 irq, irq_handler_t handler)
{
    if (irq >= 16)
        return;
    irq_handlers[irq] = handler;
    pic_unmask(irq);
}

u32 isr_breakpoint_count(void)
{
    return breakpoint_hits;
}

static void panic_screen(const struct regs *r)
{
    u32 cr2;

    cpu_cli();
    vga_set_color(VGA_WHITE, VGA_RED);
    vga_clear();

    kprintf("\n  PhoenOS - PANIQUE NOYAU\n\n");
    kprintf("  Exception %u : %s\n", r->int_no, exception_names[r->int_no]);
    kprintf("  Code d'erreur : %x\n\n", r->err_code);

    kprintf("  EAX=%x  EBX=%x  ECX=%x  EDX=%x\n", r->eax, r->ebx, r->ecx, r->edx);
    kprintf("  ESI=%x  EDI=%x  EBP=%x\n", r->esi, r->edi, r->ebp);
    kprintf("  EIP=%x  CS=%x  EFLAGS=%x\n", r->eip, r->cs, r->eflags);

    if (r->int_no == 14) {
        __asm__ volatile ("movl %%cr2, %0" : "=r"(cr2));
        kprintf("  Adresse fautive (CR2) : %x\n", cr2);
    }

    kprintf("\n  Systeme arrete. Redemarrez la machine.\n");

    for (;;)
        cpu_hlt();
}

/* Appelé depuis isr_common (isr.S) avec un pointeur sur les registres sauvés. */
void isr_handler(struct regs *r)
{
    if (r->int_no < IRQ_BASE) {
        if (r->int_no == 3) {         /* point d'arret : non fatal */
            breakpoint_hits++;
            return;
        }
        panic_screen(r);
        return;
    }

    u8 irq = (u8)(r->int_no - IRQ_BASE);

    if (pic_is_spurious(irq))
        return;

    if (irq_handlers[irq])
        irq_handlers[irq](r);

    pic_eoi(irq);
}
