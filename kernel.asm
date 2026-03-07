; ============================================================
;  kernel.asm — NarcOs Kernel v2.0
;  Yenilikler:
;    - Komut geçmişi (yukarı/aşağı ok, 8 komut)
;    - echo --red/green/yellow/cyan/white
;    - sys komutu (CPU vendor, bellek, disk)
;    - Uptime sayacı (PIT IRQ0 ile)
;    - calc komutu (toplama, çıkarma, çarpma, bölme)
;    - anim komutu (ASCII boot animasyonu)
; ============================================================
[BITS 32]
[ORG 0x10000]

VGA_BASE     equ 0xB8000
VGA_COLS     equ 80
VGA_ROWS     equ 25

; Renkler (arka plan siyah)
COLOR_NORMAL equ 0x0A   ; Açık yeşil
COLOR_BRIGHT equ 0x0B   ; Açık cyan
COLOR_DIM    equ 0x08   ; Koyu gri
COLOR_WARN   equ 0x0E   ; Sarı
COLOR_ERR    equ 0x0C   ; Açık kırmızı
COLOR_PROMPT equ 0x0A   ; Yeşil prompt
COLOR_RED    equ 0x0C
COLOR_GREEN  equ 0x0A
COLOR_YELLOW equ 0x0E
COLOR_CYAN   equ 0x0B
COLOR_WHITE  equ 0x0F
COLOR_BLUE   equ 0x09

; ── Giriş ────────────────────────────────────────────────
kernel_main:
    call init_pic
    call init_idt
    call init_pit           ; Uptime için timer
    call clear_screen
    call print_splash
    call print_meminfo
    call shell_main
    cli
    hlt

; ============================================================
;  PIC (8259A)
; ============================================================
init_pic:
    mov al, 0x11
    out 0x20, al
    out 0xA0, al
    mov al, 0x20            ; IRQ0-7  → INT 0x20-0x27
    out 0x21, al
    mov al, 0x28            ; IRQ8-15 → INT 0x28-0x2F
    out 0xA1, al
    mov al, 0x04
    out 0x21, al
    mov al, 0x02
    out 0xA1, al
    mov al, 0x01
    out 0x21, al
    out 0xA1, al
    mov al, 11111100b       ; IRQ0 (timer) + IRQ1 (klavye) açık
    out 0x21, al
    mov al, 11111111b
    out 0xA1, al
    sti
    ret

; ============================================================
;  PIT — IRQ0 Timer (uptime sayacı)
;  18.2 Hz varsayılan frekans → biz 100 Hz ayarlıyoruz
; ============================================================
init_pit:
    mov al, 0x36            ; Kanal 0, lobyte/hibyte, mod 3
    out 0x43, al
    mov ax, 11932           ; 1193182 / 100 ≈ 11932 → 100 Hz
    out 0x40, al
    mov al, ah
    out 0x40, al
    ret

; ============================================================
;  IDT
; ============================================================
init_idt:
    mov ecx, 256
    mov edi, idt_table
.fill:
    mov eax, default_handler
    mov word [edi],   ax
    mov word [edi+2], 0x08
    mov byte [edi+4], 0x00
    mov byte [edi+5], 10001110b
    shr eax, 16
    mov word [edi+6], ax
    add edi, 8
    loop .fill

    ; INT 0x20 → timer_handler
    mov edi, idt_table + (0x20 * 8)
    mov eax, timer_handler
    mov word [edi],   ax
    mov word [edi+2], 0x08
    mov byte [edi+4], 0x00
    mov byte [edi+5], 10001110b
    shr eax, 16
    mov word [edi+6], ax

    ; INT 0x21 → keyboard_handler
    mov edi, idt_table + (0x21 * 8)
    mov eax, keyboard_handler
    mov word [edi],   ax
    mov word [edi+2], 0x08
    mov byte [edi+4], 0x00
    mov byte [edi+5], 10001110b
    shr eax, 16
    mov word [edi+6], ax

    lidt [idt_descriptor]
    ret

default_handler:
    pusha
    mov al, 0x20
    out 0x20, al
    popa
    iret

; ── Timer Handler (IRQ0) ─────────────────────────────────
timer_handler:
    pusha
    inc dword [uptime_ticks]    ; Her tick'te artır (100 Hz)
    mov al, 0x20
    out 0x20, al
    popa
    iret

; ── Klavye Handler (IRQ1) ────────────────────────────────
keyboard_handler:
    pusha
    in al, 0x60
    test al, 0x80
    jnz .eoi

    ; Özel tuşlar: yukarı ok (0x48), aşağı ok (0x50)
    cmp al, 0x48
    je  .arrow_up
    cmp al, 0x50
    je  .arrow_down

    movzx eax, al
    mov al, [scancode_map + eax]
    or  al, al
    jz  .eoi

    mov ebx, [input_pos]
    cmp ebx, 126
    jge .eoi

    cmp al, 0x0D
    je  .enter
    cmp al, 0x08
    je  .backspace

    mov [input_buf + ebx], al
    inc dword [input_pos]
    mov bl, COLOR_NORMAL
    call vga_putchar
    jmp .eoi

.backspace:
    cmp dword [input_pos], 0
    je  .eoi
    dec dword [input_pos]
    mov ebx, [input_pos]
    mov byte [input_buf + ebx], 0
    call vga_backspace
    jmp .eoi

.enter:
    mov ebx, [input_pos]
    mov byte [input_buf + ebx], 0
    call save_history           ; Geçmişe kaydet
    mov byte [input_ready], 1
    call vga_newline
    jmp .eoi

.arrow_up:
    call history_prev
    jmp .eoi

.arrow_down:
    call history_next
    jmp .eoi

.eoi:
    mov al, 0x20
    out 0x20, al
    popa
    iret

; ============================================================
;  KOMUT GEÇMİŞİ (8 slot, dairesel)
; ============================================================
HIST_SIZE equ 8
HIST_LEN  equ 128

save_history:
    pusha
    ; Boş komutları kaydetme
    cmp byte [input_buf], 0
    je  .done

    ; Geçmişe yaz: history_buf + (hist_write * HIST_LEN)
    mov eax, [hist_write]
    mov ecx, HIST_LEN
    mul ecx
    mov edi, history_buf
    add edi, eax
    mov esi, input_buf
    mov ecx, HIST_LEN
    rep movsb

    ; hist_write ilerlet (dairesel)
    inc dword [hist_write]
    mov eax, [hist_write]
    cmp eax, HIST_SIZE
    jl  .no_wrap
    mov dword [hist_write], 0
.no_wrap:
    ; hist_count artır (max HIST_SIZE)
    mov eax, [hist_count]
    cmp eax, HIST_SIZE
    jge .done
    inc dword [hist_count]
.done:
    ; hist_pos = hist_write (en yeni konuma sıfırla)
    mov eax, [hist_write]
    mov [hist_pos], eax
    popa
    ret

history_prev:
    pusha
    cmp dword [hist_count], 0
    je  .done

    ; hist_pos bir geri al
    mov eax, [hist_pos]
    dec eax
    cmp eax, 0
    jge .no_wrap
    mov eax, HIST_SIZE - 1
.no_wrap:
    mov [hist_pos], eax

    ; O slottaki komutu input_buf'a kopyala
    mov ecx, HIST_LEN
    mul ecx
    mov esi, history_buf
    add esi, eax
    cmp byte [esi], 0       ; Boş slot
    je  .done

    ; Mevcut satırı temizle
    call clear_input_line

    ; Kopyala ve ekrana yaz
    mov edi, input_buf
    mov dword [input_pos], 0
.copy:
    mov al, [esi]
    or  al, al
    jz  .done
    mov [edi], al
    inc esi
    inc edi
    inc dword [input_pos]
    mov bl, COLOR_NORMAL
    call vga_putchar
    jmp .copy
.done:
    popa
    ret

history_next:
    pusha
    cmp dword [hist_count], 0
    je  .done

    mov eax, [hist_pos]
    inc eax
    cmp eax, HIST_SIZE
    jl  .no_wrap
    mov eax, 0
.no_wrap:
    mov [hist_pos], eax

    mov ecx, HIST_LEN
    mul ecx
    mov esi, history_buf
    add esi, eax

    call clear_input_line

    cmp byte [esi], 0
    je  .done

    mov edi, input_buf
    mov dword [input_pos], 0
.copy:
    mov al, [esi]
    or  al, al
    jz  .done
    mov [edi], al
    inc esi
    inc edi
    inc dword [input_pos]
    mov bl, COLOR_NORMAL
    call vga_putchar
    jmp .copy
.done:
    popa
    ret

; Mevcut input satırını VGA'dan sil ve buffer'ı temizle
clear_input_line:
    pusha
    ; input_pos kadar backspace yap
    mov ecx, [input_pos]
    or  ecx, ecx
    jz  .done
.bs:
    call vga_backspace
    loop .bs
    ; Buffer temizle
    mov dword [input_pos], 0
    mov ecx, 128
    mov edi, input_buf
    xor al, al
    rep stosb
.done:
    popa
    ret

; ============================================================
;  VGA SÜRÜCÜSÜ
; ============================================================
clear_screen:
    mov edi, VGA_BASE
    mov ecx, VGA_COLS * VGA_ROWS
    mov ax, (COLOR_NORMAL << 8) | ' '
.lp:
    mov word [edi], ax
    add edi, 2
    loop .lp
    mov dword [cursor_x], 0
    mov dword [cursor_y], 0
    call update_hw_cursor
    ret

; AL = karakter, BL = renk
vga_putchar:
    cmp al, 0x0D
    je  .cr
    mov ecx, [cursor_y]
    imul ecx, VGA_COLS
    add ecx, [cursor_x]
    shl ecx, 1
    add ecx, VGA_BASE
    mov ah, bl
    mov word [ecx], ax
    inc dword [cursor_x]
    cmp dword [cursor_x], VGA_COLS
    jl  .done
    mov dword [cursor_x], 0
    inc dword [cursor_y]
    cmp dword [cursor_y], VGA_ROWS
    jl  .done
    call vga_scroll
.done:
    call update_hw_cursor
    ret
.cr:
    mov dword [cursor_x], 0
    call update_hw_cursor
    ret

vga_putchar_al:
    mov bl, COLOR_NORMAL
    jmp vga_putchar

vga_newline:
    mov dword [cursor_x], 0
    inc dword [cursor_y]
    cmp dword [cursor_y], VGA_ROWS
    jl  .done
    call vga_scroll
.done:
    call update_hw_cursor
    ret

vga_backspace:
    cmp dword [cursor_x], 0
    je  .done
    dec dword [cursor_x]
    mov ecx, [cursor_y]
    imul ecx, VGA_COLS
    add ecx, [cursor_x]
    shl ecx, 1
    add ecx, VGA_BASE
    mov word [ecx], (COLOR_NORMAL << 8) | ' '
    call update_hw_cursor
.done:
    ret

vga_scroll:
    mov esi, VGA_BASE + VGA_COLS * 2
    mov edi, VGA_BASE
    mov ecx, VGA_COLS * (VGA_ROWS - 1)
    rep movsw
    mov ecx, VGA_COLS
    mov ax, (COLOR_NORMAL << 8) | ' '
.cl:
    mov word [edi], ax
    add edi, 2
    loop .cl
    mov dword [cursor_y], VGA_ROWS - 1
    ret

update_hw_cursor:
    mov eax, [cursor_y]
    imul eax, VGA_COLS
    add eax, [cursor_x]
    mov ecx, eax
    mov dx, 0x3D4
    mov al, 0x0F
    out dx, al
    mov dx, 0x3D5
    mov al, cl
    out dx, al
    mov dx, 0x3D4
    mov al, 0x0E
    out dx, al
    mov dx, 0x3D5
    mov al, ch
    out dx, al
    ret

; ESI = string, BL = renk
print_str:
    pusha
.lp:
    mov al, [esi]
    or  al, al
    jz  .done
    call vga_putchar
    inc esi
    jmp .lp
.done:
    popa
    ret

println:
    call print_str
    call vga_newline
    ret

; 0x0A ile bölünmüş çok satırlı string
print_multiline:
    pusha
.lp:
    mov al, [esi]
    inc esi
    or  al, al
    jz  .done
    cmp al, 0x0A
    je  .nl
    call vga_putchar
    jmp .lp
.nl:
    push esi
    call vga_newline
    pop esi
    jmp .lp
.done:
    call vga_newline
    popa
    ret

; EAX = sayı, BL = renk
; ECX kullanmaz (vga_putchar ve div çakışmasını önler)
print_dec:
    mov [pd_color], bl
    mov [pd_num], eax

    ; Sıfır özel durumu
    or  eax, eax
    jnz .build
    mov al, '0'
    mov bl, [pd_color]
    call vga_putchar
    ret

.build:
    ; dec_buf[10] = null, geriye doğru doldur
    mov byte [dec_buf+10], 0
    mov ebx, 10             ; current index
    mov eax, [pd_num]
.loop:
    xor edx, edx
    push ebx
    mov ebx, 10
    div ebx                 ; EAX=quotient, EDX=remainder
    pop ebx
    add dl, '0'
    dec ebx
    mov [dec_buf + ebx], dl
    or  eax, eax
    jnz .loop

    ; ebx = başlangıç indexi
    lea esi, [dec_buf + ebx]
.print:
    mov al, [esi]
    or  al, al
    jz  .done
    mov bl, [pd_color]
    call vga_putchar
    inc esi
    jmp .print
.done:
    ret

; ============================================================
;  SPLASH
; ============================================================
print_splash:
    mov esi, splash_border
    mov bl, COLOR_DIM
    call println
    mov esi, splash_l1
    mov bl, COLOR_BRIGHT
    call println
    mov esi, splash_l2
    call println
    mov esi, splash_l3
    call println
    mov esi, splash_l4
    call println
    mov esi, splash_l5
    call println
    mov esi, splash_l6
    call println
    mov esi, splash_name
    mov bl, COLOR_WARN
    call println
    mov esi, splash_sub
    mov bl, COLOR_DIM
    call println
    mov esi, splash_border
    call println
    mov esi, splash_hint
    mov bl, COLOR_NORMAL
    call println
    mov esi, splash_empty
    call println
    ret

; ============================================================
;  BELLEK
; ============================================================
print_meminfo:
    mov esi, mem_hdr
    mov bl, COLOR_WARN
    call print_str
    mov esi, mem_conv
    mov bl, COLOR_NORMAL
    call print_str
    mov esi, mem_conv_val
    call println
    mov esi, mem_ext
    call print_str
    mov esi, mem_ext_val
    call println
    mov esi, splash_empty
    call println
    ret

; ============================================================
;  SHELL
; ============================================================
shell_main:
.lp:
    mov esi, prompt_str
    mov bl, COLOR_PROMPT
    call print_str
    call wait_input
    call process_command
    jmp .lp

wait_input:
    mov byte [input_ready], 0
.w:
    cmp byte [input_ready], 1
    jne .w
    ret

process_command:
    cmp byte [input_buf], 0
    je  .done

    ; ── Tam eşleşmeler ──
    mov esi, input_buf
    mov edi, cmd_help
    call strcmp
    je  do_help

    mov esi, input_buf
    mov edi, cmd_clear
    call strcmp
    je  do_clear

    mov esi, input_buf
    mov edi, cmd_ver
    call strcmp
    je  do_ver

    mov esi, input_buf
    mov edi, cmd_mem
    call strcmp
    je  do_mem

    mov esi, input_buf
    mov edi, cmd_ls
    call strcmp
    je  do_ls

    mov esi, input_buf
    mov edi, cmd_date
    call strcmp
    je  do_date

    mov esi, input_buf
    mov edi, cmd_reboot
    call strcmp
    je  do_reboot

    mov esi, input_buf
    mov edi, cmd_sys
    call strcmp
    je  do_sys

    mov esi, input_buf
    mov edi, cmd_uptime
    call strcmp
    je  do_uptime

    mov esi, input_buf
    mov edi, cmd_anim
    call strcmp
    je  do_anim

    mov esi, input_buf
    mov edi, cmd_hist
    call strcmp
    je  do_hist

    ; ── Prefix eşleşmeler ──
    mov esi, input_buf
    mov edi, cmd_echo
    call strncmp
    je  do_echo

    mov esi, input_buf
    mov edi, cmd_cat
    call strncmp
    je  do_cat

    mov esi, input_buf
    mov edi, cmd_calc
    call strncmp
    je  do_calc

    ; Bilinmeyen
    mov esi, err_unk1
    mov bl, COLOR_ERR
    call print_str
    mov esi, input_buf
    call print_str
    mov esi, err_unk2
    call println

.done:
    mov dword [input_pos], 0
    mov ecx, 128
    mov edi, input_buf
    xor al, al
    rep stosb
    ret

; ── help ─────────────────────────────────────────────────
do_help:
    mov esi, help_hdr
    mov bl, COLOR_WARN
    call println
    mov esi, help_body
    mov bl, COLOR_NORMAL
    call print_multiline
    jmp process_command.done

; ── clear ────────────────────────────────────────────────
do_clear:
    call clear_screen
    call print_splash
    call print_meminfo
    jmp process_command.done

; ── ver ──────────────────────────────────────────────────
do_ver:
    mov esi, ver_str
    mov bl, COLOR_BRIGHT
    call println
    jmp process_command.done

; ── mem ──────────────────────────────────────────────────
do_mem:
    call print_meminfo
    jmp process_command.done

; ── ls ───────────────────────────────────────────────────
do_ls:
    mov esi, ls_hdr
    mov bl, COLOR_WARN
    call println
    mov esi, ls_files
    mov bl, COLOR_NORMAL
    call print_multiline
    jmp process_command.done

; ── cat ──────────────────────────────────────────────────
do_cat:
    mov esi, input_buf + 4
    mov edi, cat_f_readme
    call strcmp
    je  .readme
    mov edi, cat_f_motd
    call strcmp
    je  .motd
    mov esi, cat_notfound1
    mov bl, COLOR_ERR
    call print_str
    mov esi, input_buf + 4
    call print_str
    mov esi, cat_notfound2
    call println
    jmp process_command.done
.readme:
    mov esi, cat_readme
    mov bl, COLOR_NORMAL
    call print_multiline
    jmp process_command.done
.motd:
    mov esi, cat_motd
    mov bl, COLOR_BRIGHT
    call print_multiline
    jmp process_command.done

; ── date ─────────────────────────────────────────────────
do_date:
    mov esi, date_lbl
    mov bl, COLOR_WARN
    call print_str
    mov al, 0x04
    out 0x70, al
    in  al, 0x71
    call print_bcd
    mov al, ':'
    mov bl, COLOR_DIM
    call vga_putchar
    mov al, 0x02
    out 0x70, al
    in  al, 0x71
    call print_bcd
    mov al, ':'
    call vga_putchar
    mov al, 0x00
    out 0x70, al
    in  al, 0x71
    call print_bcd
    mov esi, date_sep
    mov bl, COLOR_DIM
    call print_str
    mov al, 0x07
    out 0x70, al
    in  al, 0x71
    call print_bcd
    mov al, '/'
    call vga_putchar
    mov al, 0x08
    out 0x70, al
    in  al, 0x71
    call print_bcd
    mov al, '/'
    call vga_putchar
    mov al, 0x09
    out 0x70, al
    in  al, 0x71
    call print_bcd
    call vga_newline
    jmp process_command.done

print_bcd:
    push eax
    push ebx
    mov bl, COLOR_NORMAL
    mov ah, al
    shr ah, 4
    and al, 0x0F
    add ah, '0'
    add al, '0'
    push eax
    mov al, ah
    call vga_putchar
    pop eax
    call vga_putchar
    pop ebx
    pop eax
    ret

; ── reboot ───────────────────────────────────────────────
do_reboot:
    mov esi, reboot_msg
    mov bl, COLOR_ERR
    call println
    mov al, 0xFE
    out 0x64, al
    hlt

; ── echo --color <metin> ─────────────────────────────────
; Sözdizimi: echo --red Merhaba
;            echo Merhaba  (varsayılan yeşil)
do_echo:
    mov esi, input_buf + 5  ; "echo " sonrası
    ; Renk flag kontrolü: "--" ile başlıyor mu?
    cmp byte [esi], '-'
    jne .plain
    cmp byte [esi+1], '-'
    jne .plain

    ; Renk adını belirle
    lea edi, [esi+2]        ; "--" sonrası

    push edi
    mov edi, echo_red
    call strcmp
    je  .do_red
    pop edi
    push edi
    mov edi, echo_green
    call strcmp
    je  .do_green
    pop edi
    push edi
    mov edi, echo_yellow
    call strcmp
    je  .do_yellow
    pop edi
    push edi
    mov edi, echo_cyan
    call strcmp
    je  .do_cyan
    pop edi
    push edi
    mov edi, echo_white
    call strcmp
    je  .do_white
    pop edi
    jmp .plain              ; Tanınmayan flag → düz yaz

.do_red:
    pop edi
    add esi, 2 + 3 + 1      ; "--red " = 6
    mov bl, COLOR_RED
    jmp .print
.do_green:
    pop edi
    add esi, 2 + 5 + 1      ; "--green " = 8
    mov bl, COLOR_GREEN
    jmp .print
.do_yellow:
    pop edi
    add esi, 2 + 6 + 1      ; "--yellow " = 9
    mov bl, COLOR_YELLOW
    jmp .print
.do_cyan:
    pop edi
    add esi, 2 + 4 + 1      ; "--cyan " = 7
    mov bl, COLOR_CYAN
    jmp .print
.do_white:
    pop edi
    add esi, 2 + 5 + 1      ; "--white " = 8
    mov bl, COLOR_WHITE
    jmp .print
.plain:
    mov bl, COLOR_NORMAL
.print:
    call println
    jmp process_command.done

; ── sys — CPU vendor + bellek + disk ─────────────────────
do_sys:
    mov esi, sys_hdr
    mov bl, COLOR_WARN
    call println

    ; CPU Vendor (CPUID leaf 0)
    mov esi, sys_cpu
    mov bl, COLOR_BRIGHT
    call print_str

    ; CPUID → EBX:EDX:ECX = vendor string (12 karakter)
    xor eax, eax
    cpuid
    ; Vendor stringi: EBX, EDX, ECX sırasıyla (her biri 4 byte)
    mov [cpu_vendor],    ebx
    mov [cpu_vendor+4],  edx
    mov [cpu_vendor+8],  ecx
    mov byte [cpu_vendor+12], 0

    mov esi, cpu_vendor
    mov bl, COLOR_NORMAL
    call println

    ; CPU Marka (CPUID leaf 0x80000002-4)
    mov esi, sys_cpu_brand
    mov bl, COLOR_BRIGHT
    call print_str

    mov eax, 0x80000002
    cpuid
    mov [cpu_brand],    eax
    mov [cpu_brand+4],  ebx
    mov [cpu_brand+8],  ecx
    mov [cpu_brand+12], edx
    mov eax, 0x80000003
    cpuid
    mov [cpu_brand+16], eax
    mov [cpu_brand+20], ebx
    mov [cpu_brand+24], ecx
    mov [cpu_brand+28], edx
    mov eax, 0x80000004
    cpuid
    mov [cpu_brand+32], eax
    mov [cpu_brand+36], ebx
    mov [cpu_brand+40], ecx
    mov [cpu_brand+44], edx
    mov byte [cpu_brand+48], 0

    mov esi, cpu_brand
    mov bl, COLOR_NORMAL
    call println

    ; Bellek
    mov esi, sys_mem
    mov bl, COLOR_BRIGHT
    call print_str
    mov esi, mem_conv_val
    mov bl, COLOR_NORMAL
    call print_str
    mov al, ' '
    call vga_putchar_al
    mov esi, sys_mem2
    call print_str
    mov esi, mem_ext_val
    call println

    ; Disk (statik — real modda disk boyutu almak gerçek BIOS gerektirir)
    mov esi, sys_disk
    mov bl, COLOR_BRIGHT
    call print_str
    mov esi, sys_disk_val
    mov bl, COLOR_NORMAL
    call println

    ; Mimari
    mov esi, sys_arch
    mov bl, COLOR_BRIGHT
    call print_str
    mov esi, sys_arch_val
    mov bl, COLOR_NORMAL
    call println

    mov esi, splash_empty
    call println
    jmp process_command.done

; ── uptime ───────────────────────────────────────────────
do_uptime:
    mov esi, uptime_lbl
    mov bl, COLOR_WARN
    call print_str

    ; toplam saniye = uptime_ticks / 100
    mov eax, [uptime_ticks]
    xor edx, edx
    mov ecx, 100
    div ecx                 ; EAX = toplam saniye

    ; saat = saniye / 3600
    xor edx, edx
    mov ecx, 3600
    div ecx                 ; EAX = saat, EDX = kalan saniye
    mov [up_h], eax
    mov eax, edx

    ; dakika = kalan / 60
    xor edx, edx
    mov ecx, 60
    div ecx                 ; EAX = dakika, EDX = saniye
    mov [up_m], eax
    mov [up_s], edx

    ; Yazdır: Xh Yd Zsn
    mov eax, [up_h]
    mov bl, COLOR_NORMAL
    call print_dec
    mov esi, uptime_h
    mov bl, COLOR_NORMAL
    call print_str

    mov eax, [up_m]
    call print_dec
    mov esi, uptime_m
    call print_str

    mov eax, [up_s]
    call print_dec
    mov esi, uptime_s
    call println

    jmp process_command.done

; ── calc ─────────────────────────────────────────────────
do_calc:
    mov esi, input_buf + 5  ; "calc " sonrası

    call parse_number
    mov [calc_a], eax

    mov al, [esi]
    inc esi
    mov [calc_op], al

    call parse_number
    mov [calc_b], eax

    mov eax, [calc_a]
    mov ebx, [calc_b]
    mov cl,  [calc_op]

    cmp cl, '+'
    je  .add
    cmp cl, '-'
    je  .sub
    cmp cl, '*'
    je  .mul
    cmp cl, '/'
    je  .div

    mov esi, calc_err
    mov bl, COLOR_ERR
    call println
    jmp process_command.done

.add:
    add eax, ebx
    jmp .result
.sub:
    sub eax, ebx
    jmp .result
.mul:
    imul eax, ebx
    jmp .result
.div:
    cmp ebx, 0
    je  .divzero
    xor edx, edx
    div ebx
    jmp .result

.divzero:
    mov esi, calc_divzero
    mov bl, COLOR_ERR
    call println
    jmp process_command.done

.result:
    ; EAX = sonuç — önce kaydet
    mov [calc_res], eax

    mov esi, calc_result
    mov bl, COLOR_BRIGHT
    call print_str

    ; Negatif mi kontrol et (bit 31)
    mov eax, [calc_res]
    test eax, 0x80000000
    jz  .pos

    ; Negatif: işaret yaz, mutlak değeri al
    push eax
    mov al, '-'
    mov bl, COLOR_ERR
    call vga_putchar
    pop eax
    neg eax

.pos:
    mov bl, COLOR_NORMAL
    call print_dec
    call vga_newline
    jmp process_command.done

; ── Sayı parse et (ESI'den ASCII rakamları oku) ──────────
parse_number:
    push ebx
    push ecx
    xor eax, eax
    xor ecx, ecx
.lp:
    mov bl, [esi]
    cmp bl, '0'
    jl  .done
    cmp bl, '9'
    jg  .done
    sub bl, '0'
    imul eax, 10
    add eax, ebx
    inc esi
    jmp .lp
.done:
    pop ecx
    pop ebx
    ret

; ── hist ─────────────────────────────────────────────────
do_hist:
    mov esi, hist_hdr
    mov bl, COLOR_WARN
    call println

    cmp dword [hist_count], 0
    je  .empty

    mov esi, 0              ; slot indeksi (ESI'yi sayaç olarak kullan)
.lp:
    mov eax, [hist_count]
    cmp esi, eax
    jge .done

    ; Satır numarası
    push esi
    mov eax, esi
    mov bl, COLOR_DIM
    call print_dec
    mov al, ' '
    call vga_putchar_al
    pop esi

    ; Geçmiş satırını yazdır
    push esi
    mov eax, esi
    mov ecx, HIST_LEN
    mul ecx
    mov edi, history_buf
    add edi, eax
    mov esi, edi
    mov bl, COLOR_NORMAL
    call println
    pop esi

    inc esi
    jmp .lp

.empty:
    mov esi, hist_empty
    mov bl, COLOR_DIM
    call println
.done:
    jmp process_command.done

; ── anim — ASCII boot animasyonu ─────────────────────────
do_anim:
    call clear_screen

    ; Frame 1
    mov esi, anim_f1
    mov bl, COLOR_DIM
    call do_anim_frame
    call anim_delay

    ; Frame 2
    call clear_screen
    mov esi, anim_f2
    mov bl, COLOR_BRIGHT
    call do_anim_frame
    call anim_delay

    ; Frame 3
    call clear_screen
    mov esi, anim_f3
    mov bl, COLOR_WARN
    call do_anim_frame
    call anim_delay

    ; Frame 4 — tam logo
    call clear_screen
    call print_splash

    jmp process_command.done

do_anim_frame:
    call print_multiline
    ret

; Basit gecikme döngüsü (~0.3 saniye)
anim_delay:
    push ecx
    mov ecx, 0x1FFFFFFF
.lp:
    loop .lp
    pop ecx
    ret

; ============================================================
;  YARDIMCI — strcmp / strncmp / parse
; ============================================================
strcmp:
    push esi
    push edi
.lp:
    mov al, [esi]
    mov bl, [edi]
    cmp al, bl
    jne .no
    or  al, al
    jz  .yes
    inc esi
    inc edi
    jmp .lp
.yes:
    pop edi
    pop esi
    xor eax, eax
    ret
.no:
    pop edi
    pop esi
    mov eax, 1
    or  eax, eax
    ret

; Prefix karşılaştırma — EDI uzunluğu kadar
strncmp:
    push esi
    push edi
    push ecx
    ; EDI uzunluğu
    mov ecx, 0
    push edi
.len:
    cmp byte [edi], 0
    je  .len_done
    inc edi
    inc ecx
    jmp .len
.len_done:
    pop edi
    push esi
.cmp:
    or  ecx, ecx
    jz  .match
    mov al, [esi]
    mov bl, [edi]
    cmp al, bl
    jne .miss
    inc esi
    inc edi
    dec ecx
    jmp .cmp
.match:
    pop esi
    pop ecx
    pop edi
    pop esi
    xor eax, eax
    ret
.miss:
    pop esi
    pop ecx
    pop edi
    pop esi
    mov eax, 1
    or  eax, eax
    ret

; ============================================================
;  VERİ
; ============================================================
splash_border db '================================================================================', 0
splash_l1 db '  ::::    :::      :::      :::::::::   ::::::::   ::::::::   ::::::::  ', 0
splash_l2 db '  :+:+:   :+:    :+: :+:    :+:    :+: :+:    :+: :+:    :+: :+:    :+: ', 0
splash_l3 db '  :+:+:+  +:+   +:+   +:+   +:+    +:+ +:+        +:+    +:+ +:+        ', 0
splash_l4 db '  +#+ +:+ +#+  +#++:++#++:  +#++:++#:  +#+        +#+    +#+  #++:++#++ ', 0
splash_l5 db '  +#+  +#+#+#  +#+     +#+  +#+    +#+ +#+    +#+ +#+    +#+        +#+ ', 0
splash_l6 db '  #+#   #+#+#  #+#     #+#  #+#    #+#  ::::::::   ::::::::  ::::::::  ', 0
splash_name db '                    NarcOs v2.0  --  x86 Assembly OS                  ', 0
splash_sub  db '             Protected Mode | IRQ | VGA | Uptime | Calc | History      ', 0
splash_hint db '  help | clear | ver | mem | ls | cat | echo | date | sys | uptime | calc | anim | hist', 0
splash_empty db ' ', 0

mem_hdr      db '[MEM] ', 0
mem_conv     db 'Konvansiyonel : ', 0
mem_conv_val db '640 KB', 0
mem_ext      db 'Genisletilmis : ', 0
mem_ext_val  db '~63 MB', 0

prompt_str   db 'NarcOs:/$ ', 0

; Komutlar
cmd_help   db 'help', 0
cmd_clear  db 'clear', 0
cmd_ver    db 'ver', 0
cmd_mem    db 'mem', 0
cmd_ls     db 'ls', 0
cmd_date   db 'date', 0
cmd_reboot db 'reboot', 0
cmd_sys    db 'sys', 0
cmd_uptime db 'uptime', 0
cmd_anim   db 'anim', 0
cmd_hist   db 'hist', 0
cmd_echo   db 'echo ', 0
cmd_cat    db 'cat ', 0
cmd_calc   db 'calc ', 0

; Help
help_hdr  db '=== NarcOs v2.0 Komut Listesi ===', 0
help_body db '  help              - Bu yardim metnini goster', 0x0A
          db '  clear             - Ekrani temizle', 0x0A
          db '  ver               - Surum bilgisi', 0x0A
          db '  mem               - Bellek bilgisi', 0x0A
          db '  ls                - Dosya listesi', 0x0A
          db '  cat <dosya>       - Dosya oku (readme, motd)', 0x0A
          db '  echo <metin>      - Metin yaz', 0x0A
          db '  echo --red/green/yellow/cyan/white <metin>', 0x0A
          db '  date              - Tarih/Saat (CMOS)', 0x0A
          db '  sys               - CPU/bellek/disk bilgisi', 0x0A
          db '  uptime            - Sistem calisma suresi', 0x0A
          db '  calc <ifade>      - Hesap makinesi (+ - * /)', 0x0A
          db '  anim              - ASCII animasyon', 0x0A
          db '  hist              - Komut gecmisi', 0x0A
          db '  reboot            - Yeniden basla', 0

; ls
ls_hdr   db '  Dosyalar:', 0
ls_files db '  drwx  ./', 0x0A
         db '  drwx  ../', 0x0A
         db '  -rw-  boot.asm       512 B', 0x0A
         db '  -rw-  stage2.asm    4096 B', 0x0A
         db '  -rw-  kernel.asm    8192 B', 0x0A
         db '  -rw-  readme         256 B', 0x0A
         db '  -rw-  motd            64 B', 0

; cat
cat_f_readme db 'readme', 0
cat_f_motd   db 'motd', 0
cat_readme:
    db '  NarcOs v2.0', 0x0A
    db '  ----------------------', 0x0A
    db '  x86 Assembly mini OS', 0x0A
    db '  Ozellikler: Bootloader, Protected Mode,', 0x0A
    db '  VGA, IRQ, Uptime, Calc, Gecmis, Renk echo', 0
cat_motd:
    db '  Hos geldin, NarcOs kullanicisi!', 0x0A
    db '  Bugun de harika bir gun.', 0x0A
    db '  Yardim: help', 0
cat_notfound1 db '  Bulunamadi: ', 0
cat_notfound2 db '  (readme, motd)', 0

; sys
sys_hdr      db '=== Sistem Bilgisi ===', 0
sys_cpu      db '  CPU Vendor  : ', 0
sys_cpu_brand db '  CPU Marka   : ', 0
sys_mem      db '  Bellek      : ', 0
sys_mem2     db '+ ', 0
sys_disk     db '  Disk        : ', 0
sys_disk_val db 'NarcOs imaji (~25 KB)', 0
sys_arch     db '  Mimari      : ', 0
sys_arch_val db 'x86 32-bit Protected Mode', 0

; uptime
uptime_lbl db '  Calisma suresi: ', 0
uptime_h   db 's ', 0
uptime_m   db 'd ', 0
uptime_s   db 'sn', 0

; calc
calc_result  db '  Sonuc: ', 0
calc_err     db '  Gecersiz operator! Kullanim: calc 2+3', 0
calc_divzero db '  Hata: Sifira bolme!', 0

; echo renk flagleri
echo_red    db 'red', 0
echo_green  db 'green', 0
echo_yellow db 'yellow', 0
echo_cyan   db 'cyan', 0
echo_white  db 'white', 0

; hist
hist_hdr   db '=== Komut Gecmisi ===', 0
hist_empty db '  (Gecmis bos)', 0

; date
date_lbl db '  Saat  : ', 0
date_sep db '  Tarih : ', 0

; diger
ver_str    db '  NarcOs v2.0 | x86 32-bit | NASM', 0
reboot_msg db '  Yeniden baslatiliyor...', 0
err_unk1   db '  Bilinmeyen komut: [', 0
err_unk2   db ']  -- "help" yazin', 0

; Anim kareleri
anim_f1:
    db '                                                                                ', 0x0A
    db '                                                                                ', 0x0A
    db '                                                                                ', 0x0A
    db '                              *                                                 ', 0x0A
    db '                             ***                                                ', 0x0A
    db '                            *****                                               ', 0x0A
    db '                           *******                                              ', 0x0A
    db '                          *********                                             ', 0x0A
    db '                         ***********                                            ', 0x0A
    db '                        *************                                           ', 0x0A
    db '                              *                                                 ', 0x0A
    db '                       NarcOs Yukleniyor...                                    ', 0

anim_f2:
    db '                                                                                ', 0x0A
    db '                          ##    ##                                              ', 0x0A
    db '                          ###   ##                                              ', 0x0A
    db '                          ####  ##                                              ', 0x0A
    db '                          ## ## ##                                              ', 0x0A
    db '                          ##  ####                                              ', 0x0A
    db '                          ##   ###                                              ', 0x0A
    db '                          ##    ##                                              ', 0x0A
    db '                                                                                ', 0x0A
    db '                       NarcOs Basliyor...                                      ', 0

anim_f3:
    db '                                                                                ', 0x0A
    db '              +===========================================+                     ', 0x0A
    db '              |                                           |                     ', 0x0A
    db '              |           N  A  R  C  O  S               |                     ', 0x0A
    db '              |                                           |                     ', 0x0A
    db '              |          x86 Assembly OS v2.0            |                     ', 0x0A
    db '              |                                           |                     ', 0x0A
    db '              |          Protected Mode | 32-bit          |                     ', 0x0A
    db '              |                                           |                     ', 0x0A
    db '              +===========================================+                     ', 0x0A
    db '                                                                                ', 0x0A
    db '                         Hazir! "help" yazin.                                  ', 0

; Scancode tablosu
scancode_map:
    db 0, 0
    db '1','2','3','4','5','6','7','8','9','0','-','=', 0x08
    db 0x09
    db 'q','w','e','r','t','y','u','i','o','p','[',']', 0x0D
    db 0
    db 'a','s','d','f','g','h','j','k','l',';', 0x27, '`'
    db 0, 0x5C
    db 'z','x','c','v','b','n','m',',','.','/'
    db 0, 0, 0, ' '
    times 70 db 0

; ── Durum değişkenleri ────────────────────────────────────
cursor_x    dd 0
cursor_y    dd 0
input_ready db 0
input_pos   dd 0
input_buf   times 128 db 0
uptime_ticks dd 0

; print_dec buffer
dec_buf  times 11 db 0
pd_color db 0
pd_num   dd 0
calc_a   dd 0
calc_b   dd 0
calc_op  db 0
calc_res dd 0

; Uptime geçicileri
up_h dd 0
up_m dd 0
up_s dd 0

; CPU bilgi tamponları
cpu_vendor  times 13 db 0
cpu_brand   times 49 db 0

; Komut geçmişi
hist_write  dd 0
hist_pos    dd 0
hist_count  dd 0
history_buf times HIST_SIZE * HIST_LEN db 0

; IDT
idt_table:
    times 256 * 8 db 0
idt_descriptor:
    dw 256 * 8 - 1
    dd idt_table
