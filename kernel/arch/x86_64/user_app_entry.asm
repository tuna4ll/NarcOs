BITS 64
default rel

section .user_code

%macro USER_GATE 2
global %1
extern %2
%1:
    call %2
%%yield_forever:
    int 0x81
    jmp %%yield_forever
%endmacro

USER_GATE user_snake_entry_gate, user_snake_entry_c
USER_GATE user_netdemo_entry_gate, user_netdemo_entry_c
USER_GATE user_fetch_entry_gate, user_fetch_entry_c
USER_GATE user_shell_entry_gate, user_shell_entry_c
USER_GATE user_narcpad_entry_gate, user_narcpad_entry_c
USER_GATE user_settings_entry_gate, user_settings_entry_c
USER_GATE user_explorer_entry_gate, user_explorer_entry_c
