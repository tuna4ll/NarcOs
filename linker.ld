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

    .user_region : ALIGN(4096)
    {
        __user_region_start = .;
        *(.user_code)
        *(.user_code.*)
        *(.user_rodata)
        *(.user_rodata.*)
        obj/apps/user_shell.o(.rodata)
        obj/apps/user_shell.o(.rodata.*)
        obj/apps/user_net_apps.o(.rodata)
        obj/apps/user_net_apps.o(.rodata.*)
        obj/apps/user_narcpad.o(.rodata)
        obj/apps/user_narcpad.o(.rodata.*)
        obj/apps/user_settings.o(.rodata)
        obj/apps/user_settings.o(.rodata.*)
        obj/apps/user_explorer.o(.rodata)
        obj/apps/user_explorer.o(.rodata.*)
        . = ALIGN(4096);
        __user_region_end = .;
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
