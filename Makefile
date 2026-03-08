# NarcOs Makefile

CC = gcc
LD = ld
AS = nasm

# CFLAGS: -m32 = 32-bit derle
#         -ffreestanding = Standart stdlib bekleme
#         -fno-pie -fcf-protection=none = Modern GCC'nin ekledigi Guvenlik tedbirlerini kapat
CFLAGS = -m32 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -fcf-protection=none -fno-builtin -mpreferred-stack-boundary=2 -mno-red-zone -Wall -Wextra -O2

# LDFLAGS: Linux ortaminda saf binary elde etmek için LD yapılandırması
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib -s --strip-all

OBJ_DIR   = obj
BOOT_DIR  = boot
KERN_DIR  = kernel

# Dosyalar
BOOT_BINS = $(BOOT_DIR)/boot.bin $(BOOT_DIR)/stage2.bin
kernel_bin_target = kernel.bin

# Object dosyalari (Siralamaya dikkat: entry.obj EN BASTA olmali)
C_SOURCES = $(wildcard $(KERN_DIR)/*.c)
C_OBJECTS = $(patsubst $(KERN_DIR)/%.c, $(OBJ_DIR)/%.o, $(C_SOURCES))
ASM_OBJECTS = $(OBJ_DIR)/entry.o

OBJ_FILES = $(ASM_OBJECTS) $(C_OBJECTS)

# Windows (MinGW) 'ld -o raw_binary_file' seklinde tam duz ciktivermeyebilir
# Buna objcopy destek cikar (Fakat biz PE-O yapisindan objcopy cekecegiz)
# Eger minios.img boyutu sacmalarsa LDFLAGS uzerinden --oformat binary zorlanabilir.

all: pre-build minios.img

pre-build:
	@mkdir -p $(OBJ_DIR)

# Bootloader kısımları (Saf binary)
$(BOOT_DIR)/boot.bin: $(BOOT_DIR)/boot.asm
	$(AS) -f bin $< -o $@

$(BOOT_DIR)/stage2.bin: $(BOOT_DIR)/stage2.asm
	$(AS) -f bin $< -o $@

# Kernel Entry (MinGW ortaminda obj formatı 'win32' olabiliyor, elf32 deneriz)
# Eger NASM elf32 destegi Windows'da yoksa win32 kullanin.
$(OBJ_DIR)/entry.o: $(KERN_DIR)/entry.asm
	$(AS) -f elf32 $< -o $@

# C Dosyalarinin derlenmesi
$(OBJ_DIR)/%.o: $(KERN_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Kernel objelerinin birlestirilmesi => Duz Binary 
$(kernel_bin_target): $(OBJ_FILES)
	$(LD) $(LDFLAGS) -o $@ $(OBJ_FILES)
	@echo "[OK] Kernel: $@ ($$(wc -c < $@) byte)"

# Disk Imajinin olusturulmasi (Mevcut mantikla ayni)
minios.img: $(BOOT_BINS) $(kernel_bin_target)
	@# Kernel kac sektor?
	$(eval KERNEL_SECS := $(shell echo $$(( ($$(wc -c < $(kernel_bin_target)) + 511) / 512 + 1 ))))
	@echo "[INFO] Kernel sektor logik sayisi: $(KERNEL_SECS)"
	dd if=/dev/zero   of=minios.img bs=512 count=2880 2>/dev/null
	dd if=$(BOOT_DIR)/boot.bin    of=minios.img bs=512 seek=0  conv=notrunc 2>/dev/null
	dd if=$(BOOT_DIR)/stage2.bin  of=minios.img bs=512 seek=1  conv=notrunc 2>/dev/null
	dd if=$(kernel_bin_target)    of=minios.img bs=512 seek=18 conv=notrunc 2>/dev/null
	@echo "[OK] minios.img hazir."

clean:
	rm -rf $(OBJ_DIR)/*.o $(BOOT_DIR)/*.bin *.img $(kernel_bin_target) kernel.tmp

run: minios.img
	qemu-system-i386 -drive file=minios.img,format=raw,index=0,media=disk
