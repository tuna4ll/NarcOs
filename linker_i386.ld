/* linker_i386.ld */
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
        obj/i386/apps/user_shell.o(.rodata)
        obj/i386/apps/user_shell.o(.rodata.*)
        obj/i386/apps/user_shell.o(.data)
        obj/i386/apps/user_shell.o(.data.*)
        obj/i386/apps/user_shell.o(.bss)
        obj/i386/apps/user_shell.o(.bss.*)
        obj/i386/apps/user_net_apps.o(.rodata)
        obj/i386/apps/user_net_apps.o(.rodata.*)
        obj/i386/apps/user_net_apps.o(.data)
        obj/i386/apps/user_net_apps.o(.data.*)
        obj/i386/apps/user_net_apps.o(.bss)
        obj/i386/apps/user_net_apps.o(.bss.*)
        obj/i386/apps/user_narcpad.o(.rodata)
        obj/i386/apps/user_narcpad.o(.rodata.*)
        obj/i386/apps/user_narcpad.o(.data)
        obj/i386/apps/user_narcpad.o(.data.*)
        obj/i386/apps/user_narcpad.o(.bss)
        obj/i386/apps/user_narcpad.o(.bss.*)
        obj/i386/apps/user_settings.o(.rodata)
        obj/i386/apps/user_settings.o(.rodata.*)
        obj/i386/apps/user_settings.o(.data)
        obj/i386/apps/user_settings.o(.data.*)
        obj/i386/apps/user_settings.o(.bss)
        obj/i386/apps/user_settings.o(.bss.*)
        obj/i386/apps/user_explorer.o(.rodata)
        obj/i386/apps/user_explorer.o(.rodata.*)
        obj/i386/apps/user_explorer.o(.data)
        obj/i386/apps/user_explorer.o(.data.*)
        obj/i386/apps/user_explorer.o(.bss)
        obj/i386/apps/user_explorer.o(.bss.*)
        obj/i386/apps/user_tls.o(.rodata)
        obj/i386/apps/user_tls.o(.rodata.*)
        obj/i386/apps/user_tls.o(.data)
        obj/i386/apps/user_tls.o(.data.*)
        obj/i386/apps/user_tls.o(.bss)
        obj/i386/apps/user_tls.o(.bss.*)
        obj/i386/apps/user_tls_crypto.o(.rodata)
        obj/i386/apps/user_tls_crypto.o(.rodata.*)
        obj/i386/apps/user_tls_crypto.o(.data)
        obj/i386/apps/user_tls_crypto.o(.data.*)
        obj/i386/apps/user_tls_crypto.o(.bss)
        obj/i386/apps/user_tls_crypto.o(.bss.*)
        obj/i386/apps/user_tls_bigint.o(.rodata)
        obj/i386/apps/user_tls_bigint.o(.rodata.*)
        obj/i386/apps/user_tls_bigint.o(.data)
        obj/i386/apps/user_tls_bigint.o(.data.*)
        obj/i386/apps/user_tls_bigint.o(.bss)
        obj/i386/apps/user_tls_bigint.o(.bss.*)
        obj/i386/apps/user_tls_x509.o(.rodata)
        obj/i386/apps/user_tls_x509.o(.rodata.*)
        obj/i386/apps/user_tls_x509.o(.data)
        obj/i386/apps/user_tls_x509.o(.data.*)
        obj/i386/apps/user_tls_x509.o(.bss)
        obj/i386/apps/user_tls_x509.o(.bss.*)
        obj/i386/apps/user_tls_pins.o(.rodata)
        obj/i386/apps/user_tls_pins.o(.rodata.*)
        obj/i386/apps/user_tls_pins.o(.data)
        obj/i386/apps/user_tls_pins.o(.data.*)
        obj/i386/apps/user_tls_pins.o(.bss)
        obj/i386/apps/user_tls_pins.o(.bss.*)
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

    ASSERT(. <= 0x000B8000, "Kernel file-backed image overlaps VGA text memory")

    . = 0x00100000;

    .bss : ALIGN(4)
    {
        __bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss*)
        __bss_end = .;
        __kernel_end = .;
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
