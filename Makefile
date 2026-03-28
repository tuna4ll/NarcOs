# NarcOs Makefile

CC = gcc
LD = ld
AS = nasm

CFLAGS = -m32 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -fcf-protection=none -fno-builtin -mpreferred-stack-boundary=2 -mno-red-zone -Wall -Wextra -O2

LDFLAGS = -m elf_i386 -T linker.ld -nostdlib -s --strip-all

OBJ_DIR   = obj
BOOT_DIR  = boot
KERN_DIR  = kernel

BOOT_BINS = $(BOOT_DIR)/boot.bin $(BOOT_DIR)/stage2.bin
kernel_bin_target = kernel.bin

C_SOURCES = $(wildcard $(KERN_DIR)/*.c)
C_OBJECTS = $(patsubst $(KERN_DIR)/%.c, $(OBJ_DIR)/%.o, $(C_SOURCES))
ASM_OBJECTS = $(OBJ_DIR)/entry.o $(OBJ_DIR)/user_test.o $(OBJ_DIR)/user_snake.o $(OBJ_DIR)/user_netdemo.o $(OBJ_DIR)/user_fetch.o

OBJ_FILES = $(ASM_OBJECTS) $(C_OBJECTS)

# Windows (MinGW) 'ld -o raw_binary_file' seklinde tam duz ciktivermeyebilir
# Buna objcopy destek cikar (Fakat biz PE-O yapisindan objcopy cekecegiz)
# Eger minios.img boyutu sacmalarsa LDFLAGS uzerinden --oformat binary zorlanabilir.

all: pre-build minios.img

pre-build:
	@mkdir -p $(OBJ_DIR)

$(BOOT_DIR)/boot.bin: $(BOOT_DIR)/boot.asm $(BOOT_DIR)/stage2.bin
	$(eval STAGE2_SECS := $(shell echo $$(( ($$(wc -c < $(BOOT_DIR)/stage2.bin) + 511) / 512 ))))
	$(AS) -DSTAGE2_SECTORS=$(STAGE2_SECS) -f bin $< -o $@

$(BOOT_DIR)/stage2.bin: $(BOOT_DIR)/stage2.asm $(kernel_bin_target)
	$(eval KERNEL_SECS := $(shell echo $$(( ($$(wc -c < $(kernel_bin_target)) + 511) / 512 ))))
	$(AS) -DKERNEL_SECTORS=$(KERNEL_SECS) -f bin $< -o $@

$(OBJ_DIR)/entry.o: $(KERN_DIR)/entry.asm
	$(AS) -f elf32 $< -o $@

$(OBJ_DIR)/user_test.o: $(KERN_DIR)/user_test.asm
	$(AS) -f elf32 $< -o $@

$(OBJ_DIR)/user_snake.o: $(KERN_DIR)/user_snake.asm
	$(AS) -f elf32 $< -o $@

$(OBJ_DIR)/user_netdemo.o: $(KERN_DIR)/user_netdemo.asm
	$(AS) -f elf32 $< -o $@

$(OBJ_DIR)/user_fetch.o: $(KERN_DIR)/user_fetch.asm
	$(AS) -f elf32 $< -o $@

$(OBJ_DIR)/%.o: $(KERN_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(kernel_bin_target): $(OBJ_FILES)
	$(LD) $(LDFLAGS) -o $@ $(OBJ_FILES)
	@echo "[OK] Kernel: $@ ($$(wc -c < $@) byte)"

minios.img: $(BOOT_BINS) $(kernel_bin_target)
	$(eval KERNEL_SECS := $(shell echo $$(( ($$(wc -c < $(kernel_bin_target)) + 511) / 512 + 1 ))))
	@echo "[INFO] Kernel sector size: $(KERNEL_SECS)"
	dd if=/dev/zero   of=minios.img bs=512 count=32768 2>/dev/null
	dd if=$(BOOT_DIR)/boot.bin    of=minios.img bs=512 seek=0  conv=notrunc 2>/dev/null
	dd if=$(BOOT_DIR)/stage2.bin  of=minios.img bs=512 seek=1  conv=notrunc 2>/dev/null
	dd if=$(kernel_bin_target)    of=minios.img bs=512 seek=18 conv=notrunc 2>/dev/null
	@echo "[OK] minios.img hazir."

clean:
	rm -rf $(OBJ_DIR)/*.o $(BOOT_DIR)/*.bin *.img $(kernel_bin_target) kernel.tmp

run-net: all
	qemu-system-i386 -m 128M -drive format=raw,file=minios.img -netdev user,id=n0 -device rtl8139,netdev=n0
