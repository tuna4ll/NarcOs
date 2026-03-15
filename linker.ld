/* linker.ld */
OUTPUT_FORMAT(binary)
ENTRY(_start)

SECTIONS
{
    . = 0x10000;

    .text : ALIGN(4096)
    {
        *(.text.prologue)
        . = ALIGN(4096);
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

    .user_code : ALIGN(16)
    {
        *(.user_code)
    }

    .bss : ALIGN(4)
    {
        *(COMMON)
        *(.bss)
        *(.bss*)
    }

    /DISCARD/ :
    {
        *(.note*)
        *(.indent)
        *(.comment)
        *(.eh_frame)
        *(.rel.plt)
    }
}
