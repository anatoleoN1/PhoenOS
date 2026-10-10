/*
 * PhoenOS - boot.S
 * Point d'entrée Multiboot (GRUB) : en-tête, pile, appel du noyau C.
 */

#define MB_MAGIC    0x1BADB002
#define MB_FLAGS    0x00000003              /* alignement modules + infos mémoire */
#define MB_CHECKSUM -(MB_MAGIC + MB_FLAGS)

    .section .multiboot, "a"
    .align 4
    .long MB_MAGIC
    .long MB_FLAGS
    .long MB_CHECKSUM

    .section .bss
    .align 16
stack_bottom:
    .skip 16384                             /* pile de 16 Kio */
stack_top:

    .section .text
    .global _start
    .type _start, @function
_start:
    cli
    movl $stack_top, %esp
    pushl $0                                /* EFLAGS propres */
    popf
    pushl %ebx                              /* pointeur sur les infos multiboot */
    pushl %eax                              /* magic fourni par le bootloader */
    call kernel_main
.Lhang:
    cli
    hlt
    jmp .Lhang
    .size _start, . - _start
