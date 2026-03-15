[BITS 32]
section .user_code
global usermode_entry_gate

align 4
user_magic_val:
    dd 0xDEADC0DE

usermode_entry_gate:
    ; ESI contains the LFB address
    ; We use EBX to store the color to avoid Ring0 data segment access issues
    mov ebx, 0xFF0000

.loop:
    mov edi, esi
    ; Draw 4 pixels at the top-left corner
    mov [edi], ebx
    mov [edi+4], ebx
    mov [edi+1024*4], ebx
    mov [edi+1024*4+4], ebx

    ; Syscall to update GUI
    mov eax, 4 ; SYS_GUI_UPDATE
    int 0x80
    
    ; Rotate color
    add ebx, 0x010203
    
    ; Delay loop
    mov ecx, 2000000
.delay:
    nop
    loop .delay
    jmp .loop
