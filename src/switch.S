/*
 * PhoenOS - switch.S
 * Commutation entre taches du noyau et premier saut en ring 3.
 *
 *   void switch_context(u32 *save_esp, u32 new_esp)
 *     Empile les registres que C attend preserves (+ EFLAGS) sur la pile COURANTE, memorise ESP dans
 *     *save_esp, prend la pile new_esp, depile ses registres et "retourne" chez elle. Une tache
 *     toute neuve a une pile fabriquee a la main (voir sched.c) : le ret mene a son point de depart.
 *
 *   void enter_user_mode(u32 entry, u32 user_esp)
 *     Ne revient jamais : charge les segments utilisateur et fait iret vers le ring 3, interruptions actives.
 *     La pile noyau que le CPU utilisera ensuite est celle de la tache (tss.esp0, regle par user.c).
 */
    .section .text

    .global switch_context
switch_context:
    movl 4(%esp), %eax              /* save_esp */
    movl 8(%esp), %edx              /* new_esp */
    pushf
    pushl %ebp
    pushl %ebx
    pushl %esi
    pushl %edi
    movl %esp, (%eax)
    movl %edx, %esp
    popl %edi
    popl %esi
    popl %ebx
    popl %ebp
    popf
    ret

    .global enter_user_mode
enter_user_mode:
    movl 4(%esp), %ecx              /* point d'entree */
    movl 8(%esp), %edx              /* pile utilisateur */
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
