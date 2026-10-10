/*
 * PhoenOS - user_enter.S
 * Passage au ring 3 et retour au noyau.
 *
 *   int user_enter(ctx, entry, user_esp, esp0_slot)
 *     Sauve les registres du noyau dans ctx (ebx, esi, edi, ebp, esp), regle la pile que le
 *     CPU utilisera quand le programme sera interrompu (juste sous la notre), puis "retourne"
 *     (iret) dans le programme avec les segments utilisateur, interruptions actives.
 *
 *   void user_leave(ctx, code)
 *     Appelee (depuis un handler) quand le programme est fini : abandonne la pile du handler,
 *     restaure ctx et fait revenir user_enter chez son appelant avec `code` dans EAX.
 */
    .section .text

    .global user_enter
user_enter:
    movl 4(%esp), %eax              /* ctx */
    movl 8(%esp), %ecx              /* point d'entree */
    movl 12(%esp), %edx             /* pile utilisateur */
    movl %ebx, 0(%eax)
    movl %esi, 4(%eax)
    movl %edi, 8(%eax)
    movl %ebp, 12(%eax)
    movl %esp, 16(%eax)             /* pointe sur l'adresse de retour */
    movl 16(%esp), %ebx             /* &tss.esp0 */
    movl %esp, (%ebx)               /* le CPU empilera sous notre adresse de retour */
    movw $0x23, %ax                 /* GDT_USER_DATA */
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    pushl $0x23                     /* SS  */
    pushl %edx                      /* ESP */
    pushl $0x202                    /* EFLAGS : IF=1, IOPL=0 */
    pushl $0x1B                     /* CS  (GDT_USER_CODE) */
    pushl %ecx                      /* EIP */
    iret

    .global user_leave
user_leave:
    movl 4(%esp), %edx
    movl 8(%esp), %eax
    movl 0(%edx), %ebx
    movl 4(%edx), %esi
    movl 8(%edx), %edi
    movl 12(%edx), %ebp
    movl 16(%edx), %esp             /* retrouve la pile de user_enter : [esp] = adresse de retour */
    pushl %eax
    movw $0x10, %ax                 /* GDT_KERNEL_DATA */
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    popl %eax
    sti
    ret
