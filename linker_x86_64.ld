OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x00010000;

    .text : ALIGN(16)
    {
        *(.text.boot)
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
        obj/x86_64/apps/user_shell.o(.rodata)
        obj/x86_64/apps/user_shell.o(.rodata.*)
        obj/x86_64/apps/user_shell.o(.data)
        obj/x86_64/apps/user_shell.o(.data.*)
        obj/x86_64/apps/user_shell.o(.bss)
        obj/x86_64/apps/user_shell.o(.bss.*)
        obj/x86_64/apps/user_net_apps.o(.rodata)
        obj/x86_64/apps/user_net_apps.o(.rodata.*)
        obj/x86_64/apps/user_net_apps.o(.data)
        obj/x86_64/apps/user_net_apps.o(.data.*)
        obj/x86_64/apps/user_net_apps.o(.bss)
        obj/x86_64/apps/user_net_apps.o(.bss.*)
        obj/x86_64/apps/user_narcpad.o(.rodata)
        obj/x86_64/apps/user_narcpad.o(.rodata.*)
        obj/x86_64/apps/user_narcpad.o(.data)
        obj/x86_64/apps/user_narcpad.o(.data.*)
        obj/x86_64/apps/user_narcpad.o(.bss)
        obj/x86_64/apps/user_narcpad.o(.bss.*)
        obj/x86_64/apps/user_settings.o(.rodata)
        obj/x86_64/apps/user_settings.o(.rodata.*)
        obj/x86_64/apps/user_settings.o(.data)
        obj/x86_64/apps/user_settings.o(.data.*)
        obj/x86_64/apps/user_settings.o(.bss)
        obj/x86_64/apps/user_settings.o(.bss.*)
        obj/x86_64/apps/user_explorer.o(.rodata)
        obj/x86_64/apps/user_explorer.o(.rodata.*)
        obj/x86_64/apps/user_explorer.o(.data)
        obj/x86_64/apps/user_explorer.o(.data.*)
        obj/x86_64/apps/user_explorer.o(.bss)
        obj/x86_64/apps/user_explorer.o(.bss.*)
        obj/x86_64/apps/user_tls.o(.rodata)
        obj/x86_64/apps/user_tls.o(.rodata.*)
        obj/x86_64/apps/user_tls.o(.data)
        obj/x86_64/apps/user_tls.o(.data.*)
        obj/x86_64/apps/user_tls.o(.bss)
        obj/x86_64/apps/user_tls.o(.bss.*)
        obj/x86_64/apps/user_tls_crypto.o(.rodata)
        obj/x86_64/apps/user_tls_crypto.o(.rodata.*)
        obj/x86_64/apps/user_tls_crypto.o(.data)
        obj/x86_64/apps/user_tls_crypto.o(.data.*)
        obj/x86_64/apps/user_tls_crypto.o(.bss)
        obj/x86_64/apps/user_tls_crypto.o(.bss.*)
        obj/x86_64/apps/user_tls_bigint.o(.rodata)
        obj/x86_64/apps/user_tls_bigint.o(.rodata.*)
        obj/x86_64/apps/user_tls_bigint.o(.data)
        obj/x86_64/apps/user_tls_bigint.o(.data.*)
        obj/x86_64/apps/user_tls_bigint.o(.bss)
        obj/x86_64/apps/user_tls_bigint.o(.bss.*)
        obj/x86_64/apps/user_tls_x509.o(.rodata)
        obj/x86_64/apps/user_tls_x509.o(.rodata.*)
        obj/x86_64/apps/user_tls_x509.o(.data)
        obj/x86_64/apps/user_tls_x509.o(.data.*)
        obj/x86_64/apps/user_tls_x509.o(.bss)
        obj/x86_64/apps/user_tls_x509.o(.bss.*)
        obj/x86_64/apps/user_tls_pins.o(.rodata)
        obj/x86_64/apps/user_tls_pins.o(.rodata.*)
        obj/x86_64/apps/user_tls_pins.o(.data)
        obj/x86_64/apps/user_tls_pins.o(.data.*)
        obj/x86_64/apps/user_tls_pins.o(.bss)
        obj/x86_64/apps/user_tls_pins.o(.bss.*)
        obj/x86_64/arch/x86_64/user_snake.o(.rodata)
        obj/x86_64/arch/x86_64/user_snake.o(.rodata.*)
        obj/x86_64/arch/x86_64/user_snake.o(.data)
        obj/x86_64/arch/x86_64/user_snake.o(.data.*)
        obj/x86_64/arch/x86_64/user_snake.o(.bss)
        obj/x86_64/arch/x86_64/user_snake.o(.bss.*)
        . = ALIGN(4096);
        __user_region_end = .;
    }

    .rodata : ALIGN(16)
    {
        *(.rodata)
        *(.rodata.*)
    }

    .data : ALIGN(16)
    {
        *(.data)
        *(.data.*)
    }

    ASSERT(. <= 0x000B8000, "x86_64 file-backed image overlaps VGA text memory")

    . = 0x00100000;

    .bss : ALIGN(16)
    {
        __bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        __bss_end = .;
        __kernel64_end = .;
    }

    /DISCARD/ :
    {
        *(.note*)
        *(.comment)
        *(.eh_frame)
    }
}

ASSERT(__user_region_end <= 0x00200000, "x86_64 user region exceeds low identity split window")
