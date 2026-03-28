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

    .user_region : ALIGN(4096)
    {
        __user_region_start = .;
        *(.user_code)
        *(.user_code.*)
        *(.user_rodata)
        *(.user_rodata.*)
        . = ALIGN(4096);
        __user_region_end = .;
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
