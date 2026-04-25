OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x40200000;

    .text : ALIGN(4096)
    {
        *(.text)
        *(.text.*)
    } :text

    .rodata : ALIGN(16)
    {
        *(.rodata)
        *(.rodata.*)
    } :text

    .data : ALIGN(4096)
    {
        *(.data)
        *(.data.*)
    } :data

    .bss : ALIGN(16)
    {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    } :data

    /DISCARD/ :
    {
        *(.note*)
        *(.comment)
        *(.eh_frame)
        *(.got*)
        *(.igot*)
        *(.plt*)
        *(.rela*)
        *(.rela.*)
        *(.rel*)
        *(.rel.*)
    }
}
