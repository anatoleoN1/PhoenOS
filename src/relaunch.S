/*
 * PhoenOS - relaunch.S : petit code de copie de l'etape 0 (voir updboot.h).
 * Il est recopie dans la memoire de preparation puis execute DE LA, car il ecrase le noyau en cours.
 * Il n'utilise que des registres et l'adresse de sa structure d'arguments : aucune adresse absolue.
 * Appel : relaunch_stub(const struct relaunch_args *a)   (ne revient jamais)
 */
    .section .text
    .global relaunch_stub_start
    .global relaunch_stub_end
relaunch_stub_start:
    movl 4(%esp), %edx
    cli
    cld
    movl 0(%edx), %esi              /* src */
    movl 4(%edx), %edi              /* dst */
    movl 8(%edx), %ecx
    shrl $2, %ecx
    movl 32(%edx), %esp             /* pile dans la memoire de preparation */
    rep movsl                       /* noyau -> adresse de chargement */
    movl 12(%edx), %edi             /* .bss a zero */
    movl 16(%edx), %ecx
    xorl %eax, %eax
    rep stosb
    movl 24(%edx), %ebx             /* infos Multiboot */
    movl 28(%edx), %eax             /* magic */
    movl 20(%edx), %ecx             /* point d'entree */
    xorl %ebp, %ebp
    xorl %esi, %esi
    xorl %edi, %edi
    jmp *%ecx
relaunch_stub_end:
