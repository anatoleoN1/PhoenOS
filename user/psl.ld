/* PhoenOS - edition de liens des bibliotheques partagees .psl : lier avec -Wl,--defsym=LIB_BASE=0x7X0000.
 * Code + constantes (lecture/execution, partages entre programmes), puis donnees (propres a chaque programme). */
ENTRY(__psl_text)
PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}
SECTIONS
{
    . = LIB_BASE;
    .text   : { __psl_text = .; *(.text*) }   :text
    .rodata : { *(.rodata*) } :text
    . = ALIGN(4096);
    .data   : { *(.data*) }   :data
    .bss    : { *(.bss*) *(COMMON) } :data
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
