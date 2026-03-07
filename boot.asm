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
