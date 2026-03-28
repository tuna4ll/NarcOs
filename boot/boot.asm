[BITS 16]
[ORG 0x7C00]
STAGE2_LOAD_SEG equ 0x0000
STAGE2_LOAD_OFF equ 0x7E00
%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 16
%endif
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [boot_drive], dl
    mov ax, 0x0003
    int 0x10
    mov si, msg_boot
    call print

    mov dword [dap_lba_lo], 1
    mov dword [dap_lba_hi], 0
    mov word [dap_count], STAGE2_SECTORS
    mov word [dap_offset], STAGE2_LOAD_OFF
    mov word [dap_segment], STAGE2_LOAD_SEG

    mov ah, 0x42
    mov dl, [boot_drive]
    mov si, dap
    int 0x13
    jc .disk_err
    mov si, msg_ok
    call print
    mov dl, [boot_drive]
    jmp STAGE2_LOAD_SEG:STAGE2_LOAD_OFF
.disk_err:
    mov si, msg_err
    call print
    jmp $
print:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print
.done:
    ret
boot_drive  db 0
dap:
    db 0x10, 0x00
dap_count:
    dw STAGE2_SECTORS
dap_offset:
    dw STAGE2_LOAD_OFF
dap_segment:
    dw STAGE2_LOAD_SEG
dap_lba_lo:
    dd 1
dap_lba_hi:
    dd 0
msg_boot    db '[BOOT] NarcOs Stage1 loading...', 0x0D, 0x0A, 0
msg_ok      db '[BOOT] Stage2 loaded. Jumping...', 0x0D, 0x0A, 0
msg_err     db '[ERR]  Disk read failed!', 0x0D, 0x0A, 0
times 510-($-$$) db 0
dw 0xAA55
