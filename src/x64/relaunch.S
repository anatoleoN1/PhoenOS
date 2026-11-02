/*
 * PhoenOS perf64 - x64/relaunch.S : code de copie de l'etape 0 des mises a jour (voir updboot.h), version 64 bits.
 * Recopie dans la memoire de preparation (sous 4 Gio, mappee en identite) puis execute DE LA : il ecrase le noyau en cours.
 * Appel : relaunch_stub(const struct relaunch_args *a)  (rdi ; ne revient jamais). Champs de la structure : updboot.h.
 *
 * Etapes : copier le nouveau noyau a son adresse de chargement, mettre son .bss a zero, repasser en mode protege 32 bits
 * (le noyau lance demarre comme sous GRUB : mode protege, sans pagination) avec eax = 0x2BADB002 et ebx = infos Multiboot 1
 * (x64/boot.S reconnait ce cas), puis sauter a son point d'entree. Aucune adresse absolue : tout est relatif a rip.
 */
    .section .text
    .code64
    .global relaunch_stub_start
    .global relaunch_stub_end
relaunch_stub_start:
    cli
    cld
    movq %rdi, %rbp                 /* arguments (sous 4 Gio) */
    movl 32(%rbp), %esp             /* pile dans la memoire de preparation (zero-etendu) */
    movl 0(%rbp), %esi              /* src */
    movl 4(%rbp), %edi              /* dst */
    movl 8(%rbp), %ecx
    rep movsb                       /* noyau -> adresse de chargement */
    movl 12(%rbp), %edi             /* .bss a zero */
    movl 16(%rbp), %ecx
    xorl %eax, %eax
    rep stosb

    /* GDT locale : 32 bits plat (0x08 code, 0x10 donnees) ; la base est ecrite a l'execution (code independant de la position) */
    leaq .Lr_gdt(%rip), %rax
    leaq .Lr_gdtr(%rip), %rcx
    movq %rax, 2(%rcx)
    lgdt (%rcx)
    pushq $0x08
    leaq .Lr_code32(%rip), %rax
    pushq %rax
    lretq                           /* CS = 0x08 : mode de compatibilite 32 bits */

    .code32
.Lr_code32:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
    movl %cr0, %eax
    andl $0x7FFFFFFF, %eax          /* pagination off (le code est en identite) */
    movl %eax, %cr0
    movl $0xC0000080, %ecx          /* EFER : mode long off */
    rdmsr
    andl $0xFFFFFEFF, %eax
    wrmsr
    movl %cr4, %eax
    andl $0xFFFFFFDF, %eax          /* PAE off */
    movl %eax, %cr4
    movl 24(%ebp), %ebx             /* infos Multiboot 1 */
    movl 28(%ebp), %eax             /* magic */
    movl 20(%ebp), %ecx             /* point d'entree */
    xorl %ebp, %ebp
    xorl %esi, %esi
    xorl %edi, %edi
    jmp *%ecx

    .align 8
.Lr_gdt:
    .quad 0
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
.Lr_gdtr:
    .short 23
    .quad 0
relaunch_stub_end:
