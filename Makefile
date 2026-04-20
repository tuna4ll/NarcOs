# NarcOs Makefile

CC = gcc
LD = ld
AS = nasm

LDFLAGS = -m elf_i386 -T linker.ld -nostdlib -s --strip-all

OBJ_DIR   = obj
BOOT_DIR  = boot
KERN_DIR  = kernel
USER_DIR  = user

BOOT_BINS = $(BOOT_DIR)/boot.bin $(BOOT_DIR)/stage2.bin
kernel_bin_target = kernel.bin
KERNEL_DIRS = $(shell find $(KERN_DIR) -type d | sort)
CFLAGS = -m32 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -fcf-protection=none -fno-builtin -fno-strict-aliasing -mpreferred-stack-boundary=2 -mno-red-zone -Wall -Wextra -O3 -fomit-frame-pointer -falign-functions=16 -falign-jumps=16 -falign-loops=16 $(addprefix -I,$(KERNEL_DIRS))
USER_CFLAGS = -m32 -ffreestanding -fno-pie -fno-pic -fno-stack-protector -fcf-protection=none -fno-builtin -fno-strict-aliasing -mpreferred-stack-boundary=2 -mno-red-zone -Wall -Wextra -O2 -fomit-frame-pointer -I$(USER_DIR)/include $(addprefix -I,$(KERNEL_DIRS))
USER_LDFLAGS = -m elf_i386 -T $(USER_DIR)/linker.ld -nostdlib -s --strip-all
USER_PROGRAMS = hello ps cat echo kill

C_SOURCES = $(shell find $(KERN_DIR) -name '*.c' | sort)
ASM_SOURCES = $(shell find $(KERN_DIR) -name '*.asm' | sort)
C_OBJECTS = $(patsubst $(KERN_DIR)/%.c, $(OBJ_DIR)/%.o, $(C_SOURCES))
ASM_OBJECTS = $(patsubst $(KERN_DIR)/%.asm, $(OBJ_DIR)/%.o, $(ASM_SOURCES))
USER_PROGRAM_OBJECTS = $(patsubst %, $(OBJ_DIR)/user/programs/%.o, $(USER_PROGRAMS))
USER_BINARIES = $(patsubst %, $(OBJ_DIR)/user/bin/%, $(USER_PROGRAMS))
USER_EMBED_OBJECTS = $(patsubst %, $(OBJ_DIR)/user/embed/%.o, $(USER_PROGRAMS))
USER_CRT_OBJECT = $(OBJ_DIR)/user/crt0.o

OBJ_FILES = $(ASM_OBJECTS) $(C_OBJECTS) $(USER_EMBED_OBJECTS)

# Windows (MinGW) 'ld -o raw_binary_file' seklinde tam duz ciktivermeyebilir
# Buna objcopy destek cikar (Fakat biz PE-O yapisindan objcopy cekecegiz)
# Eger minios.img boyutu sacmalarsa LDFLAGS uzerinden --oformat binary zorlanabilir.

.PHONY: all clean pre-build run-net user-programs
.SECONDARY: $(USER_BINARIES)

all: pre-build minios.img

user-programs: pre-build $(USER_BINARIES)

pre-build:
	@mkdir -p $(OBJ_DIR)

$(BOOT_DIR)/boot.bin: $(BOOT_DIR)/boot.asm $(BOOT_DIR)/stage2.bin
	$(eval STAGE2_SECS := $(shell echo $$(( ($$(wc -c < $(BOOT_DIR)/stage2.bin) + 511) / 512 ))))
	$(AS) -DSTAGE2_SECTORS=$(STAGE2_SECS) -f bin $< -o $@

$(BOOT_DIR)/stage2.bin: $(BOOT_DIR)/stage2.asm $(kernel_bin_target)
	$(eval KERNEL_SECS := $(shell echo $$(( ($$(wc -c < $(kernel_bin_target)) + 511) / 512 ))))
	$(AS) -DKERNEL_SECTORS=$(KERNEL_SECS) -f bin $< -o $@

$(OBJ_DIR)/%.o: $(KERN_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(KERN_DIR)/%.asm
	@mkdir -p $(dir $@)
	$(AS) -f elf32 $< -o $@

$(USER_CRT_OBJECT): $(USER_DIR)/crt0.asm
	@mkdir -p $(dir $@)
	$(AS) -f elf32 $< -o $@

$(OBJ_DIR)/user/programs/%.o: $(USER_DIR)/programs/%.c $(USER_DIR)/include/user_lib.h
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(OBJ_DIR)/user/bin/%: $(USER_CRT_OBJECT) $(OBJ_DIR)/user/programs/%.o $(USER_DIR)/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(USER_LDFLAGS) -o $@ $(filter %.o,$^)
	@echo "[OK] User: $@ ($$(wc -c < $@) byte)"

$(OBJ_DIR)/user/embed/%.o: $(OBJ_DIR)/user/bin/%
	@mkdir -p $(dir $@)
	$(LD) -r -b binary -m elf_i386 $< -o $@

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
	rm -rf $(OBJ_DIR) $(BOOT_DIR)/*.bin *.img $(kernel_bin_target) kernel.tmp

run-net: all
	qemu-system-i386 -m 128M -drive format=raw,file=minios.img -netdev user,id=n0 -device rtl8139,netdev=n0
