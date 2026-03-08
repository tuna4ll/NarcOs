/* linker.ld */
OUTPUT_FORMAT(binary)
ENTRY(_start)

SECTIONS
{
    . = 0x10000;

    .text : ALIGN(4)
    {
        *(.text.prologue)
        *(.text)
        *(.text.*)
    }

    .rodata : ALIGN(4)
    {
        *(.rodata)
        *(.rodata.*)
    }

    .data : ALIGN(4)
    {
        *(.data)
        *(.data.*)
    }

    .bss : ALIGN(4)
    {
        __bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        __bss_end = .;
    }
}
