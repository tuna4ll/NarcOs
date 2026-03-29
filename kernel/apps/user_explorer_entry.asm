[BITS 32]
section .user_code
global user_explorer_entry_gate
extern user_explorer_entry_c

user_explorer_entry_gate:
    push edi
    call user_explorer_entry_c
    add esp, 4

.yield_forever:
    int 0x81
    jmp .yield_forever
