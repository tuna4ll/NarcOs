[BITS 16]
[ORG 0x7E00]
KERNEL_OFFSET   equ 0x10000
KERNEL_LBA      equ 18
KERNEL_SECTORS  equ 256
stage2_main:
    mov [boot_drive], dl
    mov si, msg_s2
    call print16
    mov ax, 0x2401
    int 0x15
    mov si, msg_a20
    call print16
    call detect_memory
    mov si, msg_e820
    call print16
    mov ax, 0x4F00
    mov di, vbe_info_block
    int 0x10
    cmp ax, 0x004F
    jne .vbe_failed

    ; Preferred Modes List: 1080p, 1600x1200, 1280x1024, 1024x768
    mov si, preferred_modes
.find_mode_loop:
    lodsw
    or ax, ax
    jz .vbe_failed       ; End of list, no mode found
    mov cx, ax
    or cx, 0x4000        ; Set LFB bit
    mov ax, 0x4F01
    mov di, mode_info_block
    int 0x10
    cmp ax, 0x004F
    jne .find_next
    
    ; Check if it's the right resolution (Optional but safer)
    ; For now, we trust our list and BIOS fallback
    jmp .mode_found

.find_next:
    jmp .find_mode_loop
.mode_found:
    push cx
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov si, mode_info_block
    mov di, 0x6100
    mov cx, 128
    rep movsw
    pop cx
    mov si, msg_vbe_ok
    call print16
    call load_kernel
    mov si, msg_gdt
    call print16
    mov ax, 0x4F02
    mov bx, cx
    int 0x10
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_mode_entry
.vbe_failed:
    mov si, msg_vbe_err
    call print16
    call load_kernel
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_mode_entry
detect_memory:
    pusha
    xor ebx, ebx
    mov di, 0x5002
    push es
    xor ax, ax
    mov es, ax
    xor si, si
.e820_loop:
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 20
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done
    inc si
    add di, 20
    test ebx, ebx
    jnz .e820_loop
.done:
    mov word [0x5000], si
    pop es
    popa
    ret
load_kernel:
    mov si, msg_kernel
    call print16
    mov dword [dap_lba_lo], KERNEL_LBA
    mov dword [dap_lba_hi], 0
    mov word [dap_count], 1
    mov word [dap_offset], 0
    mov word [dap_segment], 0x1000
    mov word [sectors_left], KERNEL_SECTORS
    mov byte [disk_retry], 3
.read_loop:
    mov ah, 0x42
    mov dl, [boot_drive]
    mov si, dap
    int 0x13
    jnc .ok
    dec byte [disk_retry]
    jz .err
    mov ah, 0x00
    mov dl, [boot_drive]
    int 0x13
    jmp .read_loop
.ok:
    mov byte [disk_retry], 3
    inc dword [dap_lba_lo]
    add word [dap_segment], 0x0020
    dec word [sectors_left]
    jnz .read_loop
    mov si, msg_kernel_ok
    call print16
    ret
.err:
    mov si, msg_kernel_err
    call print16
    jmp $
dap:
    db 0x10, 0x00
dap_count:
    dw 1
dap_offset:
    dw 0x0000
dap_segment:
    dw 0x1000
dap_lba_lo:
    dd KERNEL_LBA
dap_lba_hi:
    dd 0
sectors_left  dw KERNEL_SECTORS
disk_retry    db 3
boot_drive    db 0
print16:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print16
.done:
    ret
align 16
gdt_start:
    dq 0
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00
gdt_end:
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start
msg_s2          db '[S2] Starting Stage 2...', 0x0D, 0x0A, 0
msg_a20         db '[S2] A20 line enabled.', 0x0D, 0x0A, 0
msg_e820        db '[S2] Memory map fetched.', 0x0D, 0x0A, 0
msg_gdt         db '[S2] GDT loading...', 0x0D, 0x0A, 0
msg_vbe_ok      db '[S2] VBE info collected.', 0x0D, 0x0A, 0
msg_vbe_err     db '[ERR] VBE initialization failed!', 0x0D, 0x0A, 0
msg_kernel      db '[S2] Loading Kernel (LBA)...', 0x0D, 0x0A, 0
msg_kernel_ok   db '[S2] Kernel loaded successfully!', 0x0D, 0x0A, 0
msg_kernel_err  db '[ERR] Kernel load failed!', 0x0D, 0x0A, 0
preferred_modes: dw 0x011B, 0x0143, 0x0118, 0x0115, 0x0112, 0 
vbe_info_block:  times 512 db 0
mode_info_block: times 256 db 0
[BITS 32]
protected_mode_entry:
    mov ax, 0x10
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x90000
    jmp 0x08:KERNEL_OFFSET
