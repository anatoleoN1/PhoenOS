/*
 * PhoenOS perf64 - x64/switch.S
 *   void switch_context(usize *save_rsp (rdi), usize new_rsp (rsi))
 *     Empile les registres que C attend preserves (rbx, rbp, r12-r15) + RFLAGS sur la pile COURANTE, memorise RSP dans
 *     *save_rsp, prend la pile new_rsp, depile ses registres et "retourne" chez elle (pile d'une tache neuve : sched.c).
 *   void enter_user_mode(usize entry (rdi), usize user_rsp (rsi))
 *     Ne revient jamais : iretq vers le ring 3, interruptions actives (programmes 64 bits : version suivante).
 */
    .section .text
    .code64

    .global switch_context
switch_context:
    pushfq
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, (%rdi)
    movq %rsi, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    popfq
    ret

    .global enter_user_mode
enter_user_mode:
    movw $0x23, %ax                 /* GDT_USER_DATA */
    movw %ax, %ds
    movw %ax, %es
    pushq $0x23                     /* SS  */
    pushq %rsi                      /* RSP */
    pushq $0x202                    /* RFLAGS : IF=1, IOPL=0 */
    pushq $0x1B                     /* CS  (GDT_USER_CODE) */
    pushq %rdi                      /* RIP */
    iretq
