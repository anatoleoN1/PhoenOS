/*
 * PhoenOS perf64 - x64/isr_stubs.S
 * Stubs d'interruption (0-31 exceptions, 32-47 IRQ, 128 appels systeme) et point d'entree commun.
 *
 * En mode long le CPU pousse TOUJOURS SS, RSP, RFLAGS, CS, RIP (et un code d'erreur pour certaines exceptions), apres avoir
 * aligne la pile sur 16 octets. Pile a l'entree de isr_common (du haut vers le bas) :
 *   [ss][rsp][rflags][cs][rip][err_code][int_no]  puis 15 registres (rax en premier ... r15 en dernier)
 * = struct regs de isr.h (r15 a l'adresse la plus basse). 22 mots de 8 octets : la pile est alignee sur 16 au `call`.
 */
    .section .text
    .code64

    .macro ISR_NOERR n
    .global isr\n
isr\n:
    pushq $0
    pushq $\n
    jmp isr_common
    .endm

    .macro ISR_ERR n
    .global isr\n
isr\n:
    pushq $\n
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

    ISR_NOERR 128                   /* int 0x80 : appels systeme */

isr_common:
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    cld
    movq %rsp, %rdi             /* argument : struct regs * */
    call isr_handler
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax
    addq $16, %rsp              /* retire int_no et err_code */
    iretq

/* Table des adresses des stubs, indexee par le numero de vecteur */
    .section .data
    .align 8
    .global isr_stub_table
isr_stub_table:
    .irp n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
    .quad isr\n
    .endr
