/* PhoenOS perf64 - x64/gdt_flush.S : void gdt_flush(const struct gdt_ptr64 *p) : charge la GDT, recharge les segments et CS. */
    .section .text
    .code64
    .global gdt_flush
    .type gdt_flush, @function
gdt_flush:
    lgdt (%rdi)
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorl %eax, %eax
    movw %ax, %fs
    movw %ax, %gs
    pushq $0x08                             /* nouveau CS */
    leaq 1f(%rip), %rax
    pushq %rax
    lretq                                   /* "retour lointain" : recharge CS */
1:  ret
    .size gdt_flush, . - gdt_flush
