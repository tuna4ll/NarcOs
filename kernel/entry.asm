[BITS 32]
global _start
extern kmain
global inb
global outb
global isr_default
global irq0_timer
global irq1_keyboard
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
outb:
    push ebp
    mov ebp, esp
    mov dx, [ebp+8]
    mov al, [ebp+12]
    out dx, al
    pop ebp
    ret
extern isr_handler_default
isr_default:
    pusha
    cld
    call isr_handler_default
    popa
    iret
extern handle_timer
irq0_timer:
    pusha
    cld
    call handle_timer
    popa
    iret
extern handle_keyboard
irq1_keyboard:
    pusha
    cld
    call handle_keyboard
    popa
    iret
global irq12_mouse
extern handle_mouse
irq12_mouse:
    pusha
    cld
    call handle_mouse
    popa
    iret
global vbe_memcpy
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
global vbe_memcpy_sse
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
