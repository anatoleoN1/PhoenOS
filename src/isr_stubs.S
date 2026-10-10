/*
 * PhoenOS - isr.S
 * Stubs d'interruption (0-31 exceptions, 32-47 IRQ) et point d'entrée commun.
 *
 * Pile à l'entrée de isr_common (du haut vers le bas) :
 *   [eflags][cs][eip][err_code][int_no]   puis pusha (8 registres)
 * Le noyau tourne en ring 0 : le CPU ne pousse ni ESP ni SS.
 */
    .section .text

/* Exception / IRQ sans code d'erreur : on pousse 0 pour garder la même forme */
    .macro ISR_NOERR n
    .global isr\n
isr\n:
    pushl $0
    pushl $\n
    jmp isr_common
    .endm

/* Exception avec code d'erreur déjà poussé par le CPU */
    .macro ISR_ERR n
    .global isr\n
isr\n:
    pushl $\n
    jmp isr_common
    .endm

    .irp n, 0,1,2,3,4,5,6,7,9,15,16,18,19,20,22,23,24,25,26,27,28,31
    ISR_NOERR \n
    .endr

    .irp n, 8,10,11,12,13,14,17,21,29,30
    ISR_ERR \n
    .endr

    .irp n, 32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
    ISR_NOERR \n
    .endr

isr_common:
    pusha
    cld
    pushl %esp                  /* argument : struct regs * */
    call isr_handler
    addl $4, %esp
    popa
    addl $8, %esp               /* retire int_no et err_code */
    iret

/* Table des adresses des stubs, indexée par le numéro de vecteur */
    .section .data
    .align 4
    .global isr_stub_table
isr_stub_table:
    .irp n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
    .long isr\n
    .endr
