/*
 * PhoenOS - isr.c
 * Dispatch des interruptions : exceptions CPU (écran de panique) et IRQ.
 */
#include "isr.h"
#include "pic.h"
#include "io.h"
#include "vga.h"
#include "kprintf.h"
#include "panic.h"
#include "log.h"

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

static const char *const selector_table[4] = { "GDT", "IDT", "LDT", "IDT" };

static irq_handler_t irq_handlers[16];
static volatile u32 breakpoint_hits;

err_t irq_install_handler(u8 irq, irq_handler_t handler)
{
    if (irq >= 16 || !handler)
        return E_INVAL;
    if (irq_handlers[irq])
        return E_BUSY;

    irq_handlers[irq] = handler;
    pic_unmask(irq);
    return E_OK;
}

u32 isr_breakpoint_count(void)
{
    return breakpoint_hits;
}

/* Explique en français ce que signifie l'exception et son code d'erreur. */
static void describe_cause(const struct regs *r)
{
    u32 e = r->err_code;

    switch (r->int_no) {
    case 0:
        kprintf("  Cause : division par zero ou quotient trop grand\n");
        break;
    case 6:
        kprintf("  Cause : instruction invalide a l'adresse %#x\n", r->eip);
        break;
    case 8:
        kprintf("  Cause : une exception s'est produite pendant le traitement d'une autre\n");
        kprintf("          (souvent : pile corrompue ou IDT/GDT invalide)\n");
        break;
    case 13:
        if (e)
            kprintf("  Cause : selecteur invalide (table %s, index %u%s)\n",
                    selector_table[(e >> 1) & 3], e >> 3, (e & 1) ? ", externe" : "");
        else
            kprintf("  Cause : violation de protection (instruction privilegiee,\n"
                    "          segment ou acces interdit)\n");
        break;
    case 14: {
        u32 cr2;
        __asm__ volatile ("movl %%cr2, %0" : "=r"(cr2));
        kprintf("  Cause : %s a l'adresse %#x (%s)\n",
                (e & 2) ? "ecriture" : "lecture", cr2,
                (e & 1) ? "droits insuffisants" : "page absente");
        break;
    }
    default:
        break;
    }
}

static void panic_exception(const struct regs *r)
{
    panic_begin("Exception du processeur");

    kprintf("  Exception %u : %s\n", r->int_no, exception_names[r->int_no]);
    kprintf("  Code d'erreur : %#x\n", r->err_code);
    describe_cause(r);

    kprintf("\n  EAX=%#x  EBX=%#x  ECX=%#x  EDX=%#x\n", r->eax, r->ebx, r->ecx, r->edx);
    kprintf("  ESI=%#x  EDI=%#x  EBP=%#x  ESP=%#x\n", r->esi, r->edi, r->ebp, r->esp_pusha);
    kprintf("  EIP=%#x  CS=%#x  EFLAGS=%#x\n", r->eip, r->cs, r->eflags);

    if (r->esp_pusha < (u32)stack_bottom || r->esp_pusha > (u32)stack_top)
        kprintf("\n  ATTENTION : ESP hors de la pile du noyau (debordement probable)\n");

    panic_backtrace(r->ebp, r->eip);
    panic_end();
}

/* Appelé depuis isr_common (isr_stubs.S) avec un pointeur sur les registres sauvés. */
void isr_handler(struct regs *r)
{
    u8 irq;

    if (r->int_no < IRQ_BASE) {
        if (r->int_no == 3) {         /* point d'arret : non fatal */
            breakpoint_hits++;
            return;
        }
        panic_exception(r);
        return;
    }

    irq = (u8)(r->int_no - IRQ_BASE);

    if (pic_is_spurious(irq))
        return;

    if (irq_handlers[irq])
        irq_handlers[irq](r);
    else
        klog(LOG_WARN, "IRQ %u recue sans handler", (u32)irq);

    pic_eoi(irq);
}
