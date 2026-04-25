[BITS 16]
[ORG 0x7E00]

KERNEL_OFFSET   equ 0x10000
KERNEL_LBA      equ 18
PML4_BASE       equ 0x9000
PDPT_BASE       equ 0xA000
PD_BASE         equ 0xB000
COM1_PORT       equ 0x3F8
PAGE_PRESENT_RW equ 0x003
PAGE_LARGE_RW   equ 0x083
IA32_EFER       equ 0xC0000080
EFER_LME        equ 0x00000100

%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 128
%endif

stage2_main:
    mov [boot_drive], dl
    call serial_init16
    mov si, msg_s2
    call print16

    mov ax, 0x2401
    int 0x15
    mov si, msg_a20
    call print16

    call detect_memory
    mov si, msg_e820
    call print16

    call detect_long_mode
    mov si, msg_long_mode_ok
    call print16

    mov ax, 0x4F00
    mov di, vbe_info_block
    int 0x10
    cmp ax, 0x004F
    jne .vbe_failed

    mov si, preferred_modes
.find_mode_loop:
    lodsw
    or ax, ax
    jz .vbe_failed
    mov cx, ax
    or cx, 0x4000
    mov ax, 0x4F01
    mov di, mode_info_block
    int 0x10
    cmp ax, 0x004F
    jne .find_next
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
    mov ax, 0x4F02
    mov bx, cx
    int 0x10
    jmp .after_vbe

.vbe_failed:
    mov si, msg_vbe_err
    call print16

.after_vbe:
    call load_kernel
    mov si, msg_gdt
    call print16
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_mode_entry

detect_long_mode:
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    xor eax, ecx
    test eax, 1 << 21
    jz .cpuid_missing

    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .long_mode_missing

    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz .long_mode_missing
    ret

.cpuid_missing:
    mov si, msg_cpuid_err
    call print16
    jmp halt_forever

.long_mode_missing:
    mov si, msg_long_mode_err
    call print16
    jmp halt_forever

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
    mov ecx, 24
    mov dword [es:di + 20], 1
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done
    inc si
    add di, 24
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
    jmp halt_forever

halt_forever:
    cli
.halt_loop:
    hlt
    jmp .halt_loop

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
    push ax
    mov ah, 0x0E
    int 0x10
    pop ax
    call serial_write_char16
    jmp print16
.done:
    ret

serial_init16:
    mov dx, COM1_PORT + 1
    xor al, al
    out dx, al
    mov dx, COM1_PORT + 3
    mov al, 0x80
    out dx, al
    mov dx, COM1_PORT + 0
    mov al, 0x03
    out dx, al
    mov dx, COM1_PORT + 1
    xor al, al
    out dx, al
    mov dx, COM1_PORT + 3
    mov al, 0x03
    out dx, al
    mov dx, COM1_PORT + 2
    mov al, 0xC7
    out dx, al
    mov dx, COM1_PORT + 4
    mov al, 0x0B
    out dx, al
    ret

serial_write_char16:
    push ax
    push dx
.wait:
    mov dx, COM1_PORT + 5
    in al, dx
    test al, 0x20
    jz .wait
    pop dx
    pop ax
    mov dx, COM1_PORT
    out dx, al
    ret

align 16
gdt_start:
    dq 0
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00
    dw 0x0000, 0x0000
    db 0x00, 10011010b, 00100000b, 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

msg_s2             db '[S2] Starting Stage 2...', 0x0D, 0x0A, 0
msg_a20            db '[S2] A20 line enabled.', 0x0D, 0x0A, 0
msg_e820           db '[S2] Memory map fetched.', 0x0D, 0x0A, 0
msg_long_mode_ok   db '[S2] Long mode supported.', 0x0D, 0x0A, 0
msg_cpuid_err      db '[ERR] CPU does not support CPUID.', 0x0D, 0x0A, 0
msg_long_mode_err  db '[ERR] CPU does not support long mode.', 0x0D, 0x0A, 0
msg_gdt            db '[S2] Entering protected mode.', 0x0D, 0x0A, 0
msg_vbe_ok         db '[S2] VBE info collected.', 0x0D, 0x0A, 0
msg_vbe_err        db '[ERR] VBE initialization failed!', 0x0D, 0x0A, 0
msg_kernel         db '[S2] Loading Kernel (LBA)...', 0x0D, 0x0A, 0
msg_kernel_ok      db '[S2] Kernel loaded successfully!', 0x0D, 0x0A, 0
msg_kernel_err     db '[ERR] Kernel load failed!', 0x0D, 0x0A, 0

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

    call setup_page_tables

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov ecx, IA32_EFER
    rdmsr
    or eax, EFER_LME
    wrmsr

    mov eax, PML4_BASE
    mov cr3, eax

    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    jmp 0x18:long_mode_entry

setup_page_tables:
    mov edi, PML4_BASE
    xor eax, eax
    mov ecx, (4096 * 3) / 4
    rep stosd

    mov dword [PML4_BASE + 0], PDPT_BASE | PAGE_PRESENT_RW
    mov dword [PML4_BASE + 4], 0

    mov dword [PDPT_BASE + 0], PD_BASE | PAGE_PRESENT_RW
    mov dword [PDPT_BASE + 4], 0

    mov dword [PD_BASE + 0], 0x00000000 | PAGE_LARGE_RW
    mov dword [PD_BASE + 4], 0
    mov dword [PD_BASE + 8], 0x00200000 | PAGE_LARGE_RW
    mov dword [PD_BASE + 12], 0
    ret

[BITS 64]
long_mode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor ax, ax
    mov fs, ax
    mov gs, ax
    mov rsp, 0x90000
    mov rbp, 0
    mov rax, KERNEL_OFFSET
    jmp rax
