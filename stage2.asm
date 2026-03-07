; ============================================================
;  stage2.asm — Stage 2: A20 Aç, GDT Kur, Protected Mode'a Geç
;  Yükleme adresi: 0x7E00
; ============================================================
[BITS 16]
[ORG 0x7E00]

KERNEL_OFFSET equ 0x10000       ; Kernel 0x10000'e yüklenecek

stage2_main:
    mov si, msg_s2
    call print16

    ; ── A20 Hattını Aç (BIOS yöntemi) ────────────────────
    mov ax, 0x2401
    int 0x15
    mov si, msg_a20
    call print16

    ; ── Kernel'i Diske'ten Yükle ──────────────────────────
    call load_kernel

    ; ── GDT Kur ──────────────────────────────────────────
    lgdt [gdt_descriptor]
    mov si, msg_gdt
    call print16

    ; ── Protected Mode'a Geç ─────────────────────────────
    cli
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Far jump — pipeline'ı temizle, CS'i 0x08 yap
    jmp 0x08:protected_mode_entry

; ── Kernel'i Yükle (sektör 18'den, 1 sektör sektör oku) ──
load_kernel:
    mov si, msg_kernel
    call print16

    mov ax, 0x1000              ; ES:BX = 0x10000
    mov es, ax
    xor bx, bx

    mov byte [cur_sector], 18
    mov byte [sectors_left], 32
    mov byte [disk_retry], 3

.read_loop:
    mov ah, 0x02
    mov al, 1                   ; Her seferinde 1 sektör
    mov ch, 0x00                ; Silindir 0
    mov cl, [cur_sector]        ; Sektör (1-tabanlı)
    mov dh, 0x00                ; Kafa 0
    mov dl, 0x80                ; İlk disk (QEMU sabit disk)
    int 0x13
    jnc .ok

    dec byte [disk_retry]
    jz .err
    mov ah, 0x00                ; Disk reset
    mov dl, 0x80
    int 0x13
    jmp .read_loop

.ok:
    mov byte [disk_retry], 3
    inc byte [cur_sector]
    add bx, 512
    jnc .no_fix
    mov ax, es
    add ax, 0x0020              ; ES'i 512 byte ilerlet (0x20 * 16 = 512)
    mov es, ax
    xor bx, bx
.no_fix:
    dec byte [sectors_left]
    jnz .read_loop

    mov si, msg_kernel_ok
    call print16
    ret
.err:
    mov si, msg_kernel_err
    call print16
    jmp $

cur_sector    db 18
sectors_left  db 32
disk_retry    db 3

; ── 16-bit print ──────────────────────────────────────────
print16:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print16
.done:
    ret

; ── GDT (Global Descriptor Table) ────────────────────────
gdt_start:
    ; Null descriptor
    dq 0

    ; Code segment: base=0, limit=4GB, exec/read, 32-bit
    dw 0xFFFF       ; limit low
    dw 0x0000       ; base low
    db 0x00         ; base mid
    db 10011010b    ; access: present, ring0, code, exec/read
    db 11001111b    ; flags: 4KB gran, 32-bit + limit high
    db 0x00         ; base high

    ; Data segment: base=0, limit=4GB, read/write, 32-bit
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b    ; access: present, ring0, data, read/write
    db 11001111b
    db 0x00

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1  ; GDT boyutu
    dd gdt_start                 ; GDT adresi

msg_s2          db '[S2] Stage 2 basliyor...', 0x0D, 0x0A, 0
msg_a20         db '[S2] A20 hatti acildi.', 0x0D, 0x0A, 0
msg_gdt         db '[S2] GDT yuklendi.', 0x0D, 0x0A, 0
msg_kernel      db '[S2] Kernel yukleniyor...', 0x0D, 0x0A, 0
msg_kernel_ok   db '[S2] Kernel yuklendi!', 0x0D, 0x0A, 0
msg_kernel_err  db '[ERR] Kernel yuklenemedi!', 0x0D, 0x0A, 0

; ── Protected Mode Giriş (32-bit) ────────────────────────
[BITS 32]
protected_mode_entry:
    ; Segment register'ları data selector ile ayarla
    mov ax, 0x10            ; GDT data segment
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x90000        ; Stack

    ; Kernel'e zıpla
    jmp 0x08:KERNEL_OFFSET
