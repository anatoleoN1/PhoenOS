/* PhoenOS - edition de liens des programmes utilisateur : charges a 4 Mio.
 * Deux segments : code + constantes (lecture/execution), puis donnees (lecture/ecriture). */
ENTRY(_start)
PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}
SECTIONS
{
    . = 0x400000;
    .text   : { *(.text*) }   :text
    .rodata : { *(.rodata*) } :text
    . = ALIGN(4096);
    .data   : { *(.data*) }   :data
    .bss    : { *(.bss*) *(COMMON) } :data
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
