/*
 * PhoenOS - gdt_flush.S
 * Charge la GDT puis recharge tous les registres de segment.
 * void gdt_flush(u32 gdt_ptr_address);
 */
    .section .text
    .global gdt_flush
    .type gdt_flush, @function
gdt_flush:
    movl 4(%esp), %eax
    lgdt (%eax)
    movw $0x10, %ax                         /* sélecteur données noyau */
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    ljmp $0x08, $.Lreload_cs                /* recharge CS (code noyau) */
.Lreload_cs:
    ret
    .size gdt_flush, . - gdt_flush
