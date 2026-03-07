; ============================================================
;  boot.asm — Stage 1 MBR Bootloader
;  Görev: Stage 2'yi diskten yükle (sektör 2-9) ve zıpla
; ============================================================
[BITS 16]
[ORG 0x7C00]

STAGE2_LOAD_SEG equ 0x0000
STAGE2_LOAD_OFF equ 0x7E00
STAGE2_SECTORS  equ 16          ; 8 KB stage2 için

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [boot_drive], dl        ; BIOS boot diskini sakla

    ; Ekranı temizle
    mov ax, 0x0003
    int 0x10

    mov si, msg_boot
    call print

    ; ── Disk okuma (INT 13h LBA benzeri CHS) ──────────────
    mov ah, 0x02
    mov al, STAGE2_SECTORS
    mov ch, 0x00                ; Silindir 0
    mov cl, 0x02                ; Sektör 2'den başla
    mov dh, 0x00                ; Kafa 0
    mov dl, [boot_drive]
    mov bx, STAGE2_LOAD_OFF
    int 0x13
    jc .disk_err

    mov si, msg_ok
    call print
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
msg_boot    db '[BOOT] NarcOs Stage1 loading...', 0x0D, 0x0A, 0
msg_ok      db '[BOOT] Stage2 loaded. Jumping...', 0x0D, 0x0A, 0
msg_err     db '[ERR]  Disk read failed!', 0x0D, 0x0A, 0

times 510-($-$$) db 0
dw 0xAA55
