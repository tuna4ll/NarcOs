[BITS 32]
section .user_code
global user_snake_entry_gate

%define SYS_UPTIME          6
%define SYS_SNAKE_GET_INPUT 11
%define SYS_SNAKE_CLOSE     12
%define SYS_RANDOM          13

%define SNAKE_PX        0
%define SNAKE_PY        400
%define SNAKE_LEN       800
%define SNAKE_APPLE_X   804
%define SNAKE_APPLE_Y   808
%define SNAKE_DEAD      812
%define SNAKE_SCORE     816
%define SNAKE_BEST      820
%define SNAKE_DIR       824
%define SNAKE_LAST_TICK 828

align 4
user_snake_magic:
    dd 0x534E4B33

user_snake_entry_gate:
    call snake_reset

.main_loop:
    mov eax, SYS_SNAKE_GET_INPUT
    int 0x80
    mov ebx, eax

    cmp ebx, 6
    je .exit_app
    cmp ebx, 5
    je .reset_game

    cmp ebx, 0
    jl .input_done
    cmp ebx, 3
    jg .input_done

    mov eax, [edi + SNAKE_DIR]
    cmp eax, 0
    jne .check_down
    cmp ebx, 1
    je .input_done
.check_down:
    cmp eax, 1
    jne .check_left
    cmp ebx, 0
    je .input_done
.check_left:
    cmp eax, 2
    jne .check_right
    cmp ebx, 3
    je .input_done
.check_right:
    cmp eax, 3
    jne .set_dir
    cmp ebx, 2
    je .input_done
.set_dir:
    mov [edi + SNAKE_DIR], ebx

.input_done:
    mov eax, SYS_UPTIME
    int 0x80
    mov esi, eax

    mov eax, [edi + SNAKE_DEAD]
    test eax, eax
    jnz .draw_and_yield

    mov eax, [edi + SNAKE_LAST_TICK]
    mov ebx, esi
    sub ebx, eax
    cmp ebx, 10
    jle .draw_and_yield

    mov [edi + SNAKE_LAST_TICK], esi

    mov ecx, [edi + SNAKE_LEN]
    dec ecx
.shift_loop:
    cmp ecx, 0
    jle .move_head
    mov eax, [edi + SNAKE_PX + ecx*4 - 4]
    mov [edi + SNAKE_PX + ecx*4], eax
    mov eax, [edi + SNAKE_PY + ecx*4 - 4]
    mov [edi + SNAKE_PY + ecx*4], eax
    dec ecx
    jmp .shift_loop

.move_head:
    mov eax, [edi + SNAKE_DIR]
    cmp eax, 0
    jne .dir_down
    dec dword [edi + SNAKE_PY]
    jmp .check_bounds
.dir_down:
    cmp eax, 1
    jne .dir_left
    inc dword [edi + SNAKE_PY]
    jmp .check_bounds
.dir_left:
    cmp eax, 2
    jne .dir_right
    dec dword [edi + SNAKE_PX]
    jmp .check_bounds
.dir_right:
    inc dword [edi + SNAKE_PX]

.check_bounds:
    mov eax, [edi + SNAKE_PX]
    cmp eax, 0
    jl .mark_dead
    cmp eax, 39
    jge .mark_dead
    mov eax, [edi + SNAKE_PY]
    cmp eax, 0
    jl .mark_dead
    cmp eax, 29
    jge .mark_dead

    mov ecx, 1
    mov ebx, [edi + SNAKE_LEN]
.self_loop:
    cmp ecx, ebx
    jge .check_apple
    mov eax, [edi + SNAKE_PX]
    cmp eax, [edi + SNAKE_PX + ecx*4]
    jne .self_next
    mov eax, [edi + SNAKE_PY]
    cmp eax, [edi + SNAKE_PY + ecx*4]
    je .mark_dead
.self_next:
    inc ecx
    jmp .self_loop

.check_apple:
    mov eax, [edi + SNAKE_PX]
    cmp eax, [edi + SNAKE_APPLE_X]
    jne .draw_and_yield
    mov eax, [edi + SNAKE_PY]
    cmp eax, [edi + SNAKE_APPLE_Y]
    jne .draw_and_yield

    mov eax, [edi + SNAKE_LEN]
    cmp eax, 100
    jge .score_only
    inc dword [edi + SNAKE_LEN]
.score_only:
    add dword [edi + SNAKE_SCORE], 10
    mov eax, [edi + SNAKE_SCORE]
    cmp eax, [edi + SNAKE_BEST]
    jle .spawn_apple
    mov [edi + SNAKE_BEST], eax

.spawn_apple:
    mov eax, SYS_RANDOM
    int 0x80
    xor edx, edx
    mov ecx, 37
    div ecx
    mov [edi + SNAKE_APPLE_X], edx
    inc dword [edi + SNAKE_APPLE_X]
    mov eax, SYS_RANDOM
    int 0x80
    xor edx, edx
    mov ecx, 27
    div ecx
    mov [edi + SNAKE_APPLE_Y], edx
    inc dword [edi + SNAKE_APPLE_Y]
    jmp .draw_and_yield

.mark_dead:
    mov dword [edi + SNAKE_DEAD], 1
    jmp .draw_and_yield

.reset_game:
    call snake_reset

.draw_and_yield:
    int 0x81
    jmp .main_loop

.exit_app:
    mov eax, SYS_SNAKE_CLOSE
    int 0x80
    int 0x81
.halt:
    jmp .halt

snake_reset:
    mov dword [edi + SNAKE_LEN], 5
    mov dword [edi + SNAKE_DEAD], 0
    mov dword [edi + SNAKE_SCORE], 0
    mov dword [edi + SNAKE_DIR], 3
    mov dword [edi + SNAKE_LAST_TICK], 0

    mov ecx, 0
    mov eax, 10
.body_init:
    cmp ecx, 5
    jge .done_reset
    mov [edi + SNAKE_PX + ecx*4], eax
    mov dword [edi + SNAKE_PY + ecx*4], 10
    dec eax
    inc ecx
    jmp .body_init
.done_reset:
    mov eax, SYS_RANDOM
    int 0x80
    xor edx, edx
    mov ecx, 37
    div ecx
    mov [edi + SNAKE_APPLE_X], edx
    inc dword [edi + SNAKE_APPLE_X]

    mov eax, SYS_RANDOM
    int 0x80
    xor edx, edx
    mov ecx, 27
    div ecx
    mov [edi + SNAKE_APPLE_Y], edx
    inc dword [edi + SNAKE_APPLE_Y]
    ret
