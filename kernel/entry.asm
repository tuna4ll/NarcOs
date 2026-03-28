[BITS 32]
global _start
extern kmain
global inb
global inw
global inl
global outb
global outw
global outl
global isr_default
global irq0_timer
global irq1_keyboard
global vbe_memcpy
global vbe_memcpy_sse
global vbe_memset_sse
global vbe_alpha_blend_sse
global gdt_flush
global tss_flush
global isr_syscall
global isr_user_yield
global jump_to_usermode
global jump_to_usermode_v9
global isr_gpf
global isr_double_fault
global load_page_directory
global enable_paging
global process_switch
global run_user_task
extern usermode_jump_eip
extern usermode_jump_esp
extern user_kernel_resume_esp
extern user_kernel_ebx
extern user_kernel_esi
extern user_kernel_edi
extern user_kernel_ebp
SECTION .text.prologue
_start:
    mov esp, 0x2800000
    mov ebp, esp
    mov eax, cr0
    and ax, 0xFFFB
    or ax, 0x2
    mov cr0, eax
    mov eax, cr4
    or ax, 3 << 9
    mov cr4, eax
    call kmain
    cli
.halt:
    hlt
    jmp .halt

SECTION .text
inb:
    push ebp
    mov ebp, esp
    mov dx, [ebp+8]
    in al, dx
    pop ebp
    ret
inw:
    push ebp
    mov ebp, esp
    mov dx, [ebp+8]
    in ax, dx
    pop ebp
    ret
inl:
    push ebp
    mov ebp, esp
    mov dx, [ebp+8]
    in eax, dx
    pop ebp
    ret
outb:
    push ebp
    mov ebp, esp
    mov dx, [ebp+8]
    mov al, [ebp+12]
    out dx, al
    pop ebp
    ret
outw:
    push ebp
    mov ebp, esp
    mov dx, [ebp+8]
    mov ax, [ebp+12]
    out dx, ax
    pop ebp
    ret
outl:
    push ebp
    mov ebp, esp
    mov dx, [ebp+8]
    mov eax, [ebp+12]
    out dx, eax
    pop ebp
    ret
extern isr_handler_default
isr_default:
    pusha
    
    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax

    cld
    call isr_handler_default

    pop eax
    mov ds, ax
    mov es, ax

    popa
    iret
extern handle_timer
irq0_timer:
    pusha
    
    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    cld
    call handle_timer

    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa
    iret
extern handle_keyboard
irq1_keyboard:
    pusha
    
    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    cld
    call handle_keyboard

    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa
    iret
global irq12_mouse
extern handle_mouse
irq12_mouse:
    pusha
    
    mov ax, ds
    push eax
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    cld
    call handle_mouse

    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa
    iret

gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.flush
.flush:
    ret

tss_flush:
    mov ax, 0x28 ; Index 5 * 8 = 40 (0x28). TSS is Ring 0.
    ltr ax
    ret

load_page_directory:
    mov eax, [esp + 4]
    mov cr3, eax
    ret

enable_paging:
    mov eax, cr4
    or eax, 0x10
    mov cr4, eax
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax
    ret

process_switch:
    mov eax, [esp + 4]
    mov edx, [esp + 8]
    push ebp
    push ebx
    push esi
    push edi
    mov [eax], esp
    mov esp, edx
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

run_user_task:
    mov [user_kernel_resume_esp], esp
    mov [user_kernel_ebx], ebx
    mov [user_kernel_esi], esi
    mov [user_kernel_edi], edi
    mov [user_kernel_ebp], ebp
    mov ebp, [esp + 4]

    push dword [ebp + 68]
    push dword [ebp + 64]
    push dword [ebp + 60]
    push dword [ebp + 56]
    push dword [ebp + 52]

    mov ax, [ebp + 12]
    mov ds, ax
    mov ax, [ebp + 8]
    mov es, ax
    mov ax, [ebp + 4]
    mov fs, ax
    mov ax, [ebp + 0]
    mov gs, ax

    mov edi, [ebp + 16]
    mov esi, [ebp + 20]
    mov edx, [ebp + 36]
    mov ecx, [ebp + 40]
    mov ebx, [ebp + 32]
    mov eax, [ebp + 44]
    mov ebp, [ebp + 24]
    iret

extern syscall_handler
extern user_yield_handler

SECTION .text

jump_to_usermode_v9:
    ; Parameters on stack: [esp+4]=EIP, [esp+8]=ESP, [esp+12]=LFB
    mov eax, [esp + 4]
    mov ebx, [esp + 8]
    mov esi, [esp + 12] ; Pass LFB address in ESI to usermode
    
    cli
    
    ; Setup IRET frame
    push dword 0x23    ; SS (User Data)
    push ebx           ; ESP (User Stack)
    push dword 0x202   ; EFLAGS (IF=1) - FULL SYSTEM TEST
    push dword 0x1B    ; CS (User Code)
    push eax           ; EIP (Entry Point)
    
    ; Load segments
    mov cx, 0x23 
    mov ds, cx
    mov es, cx
    mov fs, cx
    mov gs, cx

    iret


isr_syscall:
    push dword 0 ; Dummy error code
    pusha
    
    ; Save old segments
    push ds
    push es
    push fs
    push gs
    
    ; Load kernel segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Pass pointer to stack frame [trap_frame_t]
    push esp
    call syscall_handler
    add esp, 4

    ; Restore segments
    pop gs
    pop fs
    pop es
    pop ds

    popa
    add esp, 4 ; remove dummy error code
    iret

isr_user_yield:
    push dword 0
    pusha

    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp
    call user_yield_handler
    add esp, 4

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov esp, [user_kernel_resume_esp]
    mov ebx, [user_kernel_ebx]
    mov esi, [user_kernel_esi]
    mov edi, [user_kernel_edi]
    mov ebp, [user_kernel_ebp]
    ret

extern gpf_handler
isr_gpf:
    ; Hardware pushed error code already
    pusha
    
    ; Save old segments
    push ds
    push es
    push fs
    push gs
    
    ; Load kernel segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp ; Pointer to trap_frame_t
    call gpf_handler
    add esp, 4
    
    pop gs
    pop fs
    pop es
    pop ds
    
    popa
    add esp, 4 ; Error code
    iret

isr_double_fault:
    cli
    hlt
    jmp isr_double_fault
vbe_memcpy:
    push ebp
    mov ebp, esp
    push edi
    push esi
    push ecx
    mov edi, [ebp+8]
    mov esi, [ebp+12]
    mov ecx, [ebp+16]
    mov eax, ecx
    shr ecx, 2
    cld
    rep movsd
    mov ecx, eax
    and ecx, 3
    rep movsb
    pop ecx
    pop esi
    pop edi
    pop ebp
    ret
vbe_memcpy_sse:
    push ebp
    mov ebp, esp
    push edi
    push esi
    push ecx
    mov edi, [ebp+8]
    mov esi, [ebp+12]
    mov ecx, [ebp+16]
    mov eax, ecx
    shr ecx, 4
    jz .tail_4
.loop_16:
    movups xmm0, [esi]
    movups [edi], xmm0
    add esi, 16
    add edi, 16
    dec ecx
    jnz .loop_16
.tail_4:
    mov ecx, eax
    and ecx, 15
    shr ecx, 2
    jz .tail_1
    rep movsd
.tail_1:
    mov ecx, eax
    and ecx, 3
    rep movsb
    pop ecx
    pop esi
    pop edi
    pop ebp
    ret

vbe_memset_sse:
    push ebp
    mov ebp, esp
    push edi
    push ecx
    mov edi, [ebp+8]
    mov eax, [ebp+12]
    mov ecx, [ebp+16]
    
    movd xmm0, eax
    pshufd xmm0, xmm0, 0 ; Fill xmm0 with the 32-bit color

    mov edx, ecx
    shr ecx, 4
    jz .memset_tail
.memset_loop:
    movups [edi], xmm0
    add edi, 16
    dec ecx
    jnz .memset_loop
.memset_tail:
    mov ecx, edx
    and ecx, 15
    shr ecx, 2
    jz .memset_tail_1
    rep stosd
.memset_tail_1:
    mov ecx, edx
    and ecx, 3
    rep stosb
    pop ecx
    pop edi
    pop ebp
    ret

vbe_alpha_blend_sse:
    push ebp
    mov ebp, esp
    push edi
    push esi
    push ecx
    
    mov edi, [ebp+8]
    mov eax, [ebp+12]
    mov edx, [ebp+16]
    mov ecx, [ebp+20]
    
    test ecx, ecx
    jz .alpha_done
    
    pxor xmm7, xmm7
    
    movd xmm0, eax
    punpcklbw xmm0, xmm7
    pshufd xmm0, xmm0, 0
    
    movd xmm1, edx
    pshuflw xmm1, xmm1, 0
    pshufd xmm1, xmm1, 0
    
    mov eax, 256
    sub eax, edx
    movd xmm2, eax
    pshuflw xmm2, xmm2, 0
    pshufd xmm2, xmm2, 0

.alpha_loop:
    cmp ecx, 4
    jl .alpha_tail
    
    movups xmm3, [edi]
    movaps xmm4, xmm3
    punpcklbw xmm4, xmm7
    punpckhbw xmm3, xmm7
    
    pmullw xmm4, xmm2
    movaps xmm5, xmm0
    pmullw xmm5, xmm1
    paddw xmm4, xmm5
    psrlw xmm4, 8
    
    pmullw xmm3, xmm2
    movaps xmm5, xmm0
    pmullw xmm5, xmm1
    paddw xmm3, xmm5
    psrlw xmm3, 8
    
    packuswb xmm4, xmm3
    movups [edi], xmm4
    
    add edi, 16
    sub ecx, 4
    jnz .alpha_loop
    jmp .alpha_done

.alpha_tail:
    test ecx, ecx
    jz .alpha_done
.alpha_tail_loop:
    ; Single pixel fallback (simplified)
    mov eax, [edi]
    ; eax is dst, xmm0[0] is src
    ; This part is a bit slow in asm without SIMD, 
    ; but we only do it for 1-3 pixels.
    ; For now, just skip tail to keep it simple, 
    ; or handle it in C.
    add edi, 4
    dec ecx
    jnz .alpha_tail_loop

.alpha_done:
    pop ecx
    pop esi
    pop edi
    pop ebp
    ret
