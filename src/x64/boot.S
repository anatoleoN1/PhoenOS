/*
 * PhoenOS perf64 - x64/boot.S
 * Point d'entree Multiboot2 (GRUB, en BIOS comme en UEFI) : le bootloader nous lance en mode protege 32 bits,
 * sans pagination. On prepare la pagination (4 Gio en identite, pages de 2 Mio), on passe en mode long et on appelle
 * kernel_main (C, 64 bits). Les infos Multiboot2 sont d'abord converties au format Multiboot 1 (x64/mb2.c) que le
 * reste du noyau sait lire, et les infos de l'ecran (framebuffer) sont gardees a part.
 */

#define MB2_MAGIC    0xE85250D6
#define MB2_ARCH     0                      /* i386 (mode protege 32 bits) */
#define MB2_LOADER   0x36D76289             /* valeur de eax donnee par un bootloader Multiboot2 */

    .section .multiboot, "a"
    .align 8
mb2_header:
    .long MB2_MAGIC
    .long MB2_ARCH
    .long mb2_header_end - mb2_header
    .long -(MB2_MAGIC + MB2_ARCH + (mb2_header_end - mb2_header))

    /* ecran graphique souhaite : 1280x800, 32 bits (= 80x25 caracteres de 16x32). Le bootloader prend le plus proche. */
    .align 8
    .short 5                                /* type : framebuffer */
    .short 1                                /* flags : optionnel (sans ecran graphique, on aura le texte VGA) */
    .long 20
    .long 1280
    .long 800
    .long 32

    .align 8
    .short 0                                /* fin des tags */
    .short 0
    .long 8
mb2_header_end:

    .section .bss
    .align 4096
    .global boot_pml4
boot_pml4:  .skip 4096
boot_pdpt:  .skip 4096
    .global boot_pd
boot_pd:    .skip 4096 * 4                  /* 4 repertoires de 512 entrees de 2 Mio = 4 Gio */

    .align 16
    .global stack_bottom
stack_bottom:
    .skip 32768                             /* pile de 32 Kio */
    .global stack_top
stack_top:

    .section .data
    .align 8
mb2_info_phys:  .long 0
boot_relaunched: .long 0

    .align 8
boot_gdt:
    .quad 0                                 /* 0x00 : nul */
    .quad 0x00AF9A000000FFFF                /* 0x08 : code 64 bits, ring 0 */
    .quad 0x00CF92000000FFFF                /* 0x10 : donnees, ring 0 */
boot_gdt_end:
boot_gdt_ptr:
    .short boot_gdt_end - boot_gdt - 1
    .long boot_gdt

    .section .rodata
msg_no_mb2:  .asciz "PhoenOS perf64 : demarrez avec un bootloader Multiboot2 (GRUB)"
msg_no_long: .asciz "PhoenOS perf64 : ce processeur n'a pas le mode 64 bits (utilisez l'edition perf32)"

    .section .text
    .code32
    .global _start
    .type _start, @function
_start:
    cli
    cld
    movl $stack_top, %esp
    cmpl $MB2_LOADER, %eax
    je   1f
    cmpl $0x2BADB002, %eax                  /* noyau relance par une mise a jour (etape 0) : infos en Multiboot 1 */
    jne  .Lno_mb2
    movl $1, boot_relaunched
1:  movl %ebx, mb2_info_phys

    /* le processeur sait-il le mode long ? (CPUID 0x80000001, EDX bit 29) */
    movl $0x80000000, %eax
    cpuid
    cmpl $0x80000001, %eax
    jb   .Lno_long
    movl $0x80000001, %eax
    cpuid
    testl $(1 << 29), %edx
    jz   .Lno_long

    /* PML4[0] -> PDPT ; PDPT[0..3] -> 4 repertoires ; repertoires : pages de 2 Mio (P, RW, PS) ;
     * au-dela de 1 Gio : cache desactive (PCD+PWT) = peripheriques, ecran, registres PCI */
    movl $boot_pdpt, %eax
    orl  $0x3, %eax
    movl %eax, boot_pml4
    movl $0, boot_pml4 + 4
    xorl %ecx, %ecx
.Lpdpt:
    movl %ecx, %eax
    shll $12, %eax
    addl $boot_pd, %eax
    orl  $0x3, %eax
    movl %eax, boot_pdpt(, %ecx, 8)
    movl $0, boot_pdpt + 4(, %ecx, 8)
    incl %ecx
    cmpl $4, %ecx
    jne  .Lpdpt

    xorl %ecx, %ecx
.Lpd:
    movl %ecx, %eax
    shll $21, %eax                          /* adresse de la page de 2 Mio */
    orl  $0x83, %eax
    cmpl $512, %ecx
    jb   1f
    orl  $0x18, %eax
1:  movl %eax, boot_pd(, %ecx, 8)
    movl $0, boot_pd + 4(, %ecx, 8)
    incl %ecx
    cmpl $2048, %ecx
    jne  .Lpd

    movl $boot_pml4, %eax
    movl %eax, %cr3
    movl %cr4, %eax
    orl  $0x20, %eax                        /* PAE */
    movl %eax, %cr4
    movl $0xC0000080, %ecx                  /* EFER */
    rdmsr
    orl  $0x100, %eax                       /* LME : mode long active */
    wrmsr
    movl %cr0, %eax
    orl  $0x80010001, %eax                  /* PG + WP + PE */
    movl %eax, %cr0
    lgdt boot_gdt_ptr
    ljmp $0x08, $long_mode

.Lno_mb2:
    movl $msg_no_mb2, %esi
    jmp  .Lfail
.Lno_long:
    movl $msg_no_long, %esi
.Lfail:                                     /* message en texte VGA (ligne 0), puis arret */
    movl $0xB8000, %edi
    movb $0x4F, %ah
1:  lodsb
    testb %al, %al
    jz   2f
    movw %ax, (%edi)
    addl $2, %edi
    jmp  1b
2:  cli
    hlt
    jmp  2b
    .size _start, . - _start

    .code64
long_mode:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorl %eax, %eax
    movw %ax, %fs
    movw %ax, %gs
    movl $stack_top, %esp                   /* (zero-etendu dans rsp) */
    xorl %ebp, %ebp                         /* fin de la chaine de frames (trace d'appel) */
    pushq $0
    popfq                                   /* RFLAGS propres */
    movl mb2_info_phys, %edi
    cmpl $0, boot_relaunched
    jne  1f
    call mb2_convert                        /* rax = struct multiboot_info (format Multiboot 1) */
    jmp  2f
1:  call mb1_adopt                          /* deja en Multiboot 1 : on garde tel quel */
2:
    movl $0x2BADB002, %edi                  /* "magic" Multiboot 1 : le reste du noyau ne voit pas la difference */
    movq %rax, %rsi
    call kernel_main
    call kernel_main_returned               /* ne devrait jamais arriver : panique */
.Lhang:
    cli
    hlt
    jmp .Lhang
