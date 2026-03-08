; ============================================================
; entry.asm
; ============================================================
[BITS 32]
global _start
extern kmain


global inb
global outb


global isr_default
global irq0_timer
global irq1_keyboard

SECTION .text.prologue

; -----------------------------------------
; _start
; -----------------------------------------
_start:
    mov esp, 0x90000
    mov ebp, esp


    call kmain


    cli
.halt:
    hlt
    jmp .halt

SECTION .text

; -----------------------------------------
; I/O Functions
; -----------------------------------------
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

; -----------------------------------------
; Exceptions
; -----------------------------------------

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
