/* PhoenOS - edition de liens des programmes utilisateur : charges a 4 Mio. */
ENTRY(_start)
SECTIONS
{
    . = 0x400000;
    .text   : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(.bss*) *(COMMON) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
