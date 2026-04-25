# NarcOs Makefile

CC = gcc
LD = ld
AS = nasm
OBJCOPY = objcopy

OBJ_DIR  = obj
BOOT_DIR = boot
KERN_DIR = kernel
USER_DIR = user

KERNEL_DIRS = $(shell find $(KERN_DIR) -type d | sort)
USER_PROGRAMS = hello ps cat echo kill proc_test pipe_test

KERNEL_INCLUDE_FLAGS = $(addprefix -I,$(KERNEL_DIRS))
COMMON_CFLAGS = -ffreestanding -fno-pie -fno-pic -fno-stack-protector -fcf-protection=none -fno-builtin -fno-strict-aliasing -Wall -Wextra
COMMON_USER_CFLAGS = -ffreestanding -fno-pie -fno-pic -fno-stack-protector -fcf-protection=none -fno-builtin -fno-strict-aliasing -Wall -Wextra -I$(USER_DIR)/include

I386_OBJ_DIR = $(OBJ_DIR)/i386
I386_CFLAGS = -m32 $(COMMON_CFLAGS) $(KERNEL_INCLUDE_FLAGS) -mpreferred-stack-boundary=2 -mno-red-zone -O3 -fomit-frame-pointer -falign-functions=16 -falign-jumps=16 -falign-loops=16
I386_USER_CFLAGS = -m32 $(COMMON_USER_CFLAGS) $(KERNEL_INCLUDE_FLAGS) -mpreferred-stack-boundary=2 -mno-red-zone -O2 -fomit-frame-pointer
I386_LDFLAGS = -m elf_i386 -T linker_i386.ld -nostdlib -s --strip-all
I386_USER_LDFLAGS = -m elf_i386 -T $(USER_DIR)/linker.ld -nostdlib -s --strip-all
I386_C_SOURCES = $(shell find $(KERN_DIR) -name '*.c' ! -path '$(KERN_DIR)/arch/x86_64/*' | sort)
I386_ASM_SOURCES = $(shell find $(KERN_DIR) -name '*.asm' ! -path '$(KERN_DIR)/arch/x86_64/*' | sort)
I386_C_OBJECTS = $(patsubst $(KERN_DIR)/%.c,$(I386_OBJ_DIR)/%.o,$(I386_C_SOURCES))
I386_ASM_OBJECTS = $(patsubst $(KERN_DIR)/%.asm,$(I386_OBJ_DIR)/%.o,$(I386_ASM_SOURCES))
I386_USER_PROGRAM_OBJECTS = $(patsubst %,$(I386_OBJ_DIR)/user/programs/%.o,$(USER_PROGRAMS))
I386_USER_BINARIES = $(patsubst %,$(I386_OBJ_DIR)/user/bin/%,$(USER_PROGRAMS))
I386_USER_EMBED_OBJECTS = $(patsubst %,$(I386_OBJ_DIR)/user/embed/%.o,$(USER_PROGRAMS))
I386_USER_CRT_OBJECT = $(I386_OBJ_DIR)/user/crt0.o
I386_KERNEL_OBJECTS = $(I386_ASM_OBJECTS) $(I386_C_OBJECTS) $(I386_USER_EMBED_OBJECTS)
I386_BOOT_BIN = $(I386_OBJ_DIR)/boot/boot.bin
I386_STAGE2_BIN = $(I386_OBJ_DIR)/boot/stage2.bin
I386_KERNEL_BIN = $(I386_OBJ_DIR)/kernel.bin
I386_IMAGE = $(I386_OBJ_DIR)/minios.img

X86_64_OBJ_DIR = $(OBJ_DIR)/x86_64
X86_64_CFLAGS = -m64 $(COMMON_CFLAGS) -I$(KERN_DIR)/arch/x86_64 $(KERNEL_INCLUDE_FLAGS) -mno-red-zone -mgeneral-regs-only -mno-mmx -mno-sse -mno-sse2 -msoft-float -O2 -fomit-frame-pointer
X86_64_USER_CFLAGS = -m64 $(COMMON_USER_CFLAGS) -I$(KERN_DIR)/arch/x86_64 $(KERNEL_INCLUDE_FLAGS) -mno-red-zone -mgeneral-regs-only -mno-mmx -mno-sse -mno-sse2 -msoft-float -O2 -fomit-frame-pointer
X86_64_LDFLAGS = -m elf_x86_64 -T linker_x86_64.ld -nostdlib
X86_64_ALL_C_SOURCES = $(shell find $(KERN_DIR) -name '*.c' | sort)
X86_64_C_SOURCES = $(filter-out \
	$(KERN_DIR)/arch/x86/% \
	$(KERN_DIR)/arch/x86_64/display.c \
	$(KERN_DIR)/arch/x86_64/main.c \
	$(KERN_DIR)/arch/x86_64/stub.c \
	$(KERN_DIR)/arch/x86_64/stubs.c \
	$(KERN_DIR)/arch/x86_64/usermode.c \
	$(KERN_DIR)/drivers/platform/serial.c \
	$(KERN_DIR)/mm/memory_alloc.c, \
	$(X86_64_ALL_C_SOURCES))
X86_64_ALL_ASM_SOURCES = $(shell find $(KERN_DIR) -name '*.asm' | sort)
X86_64_ASM_SOURCES = $(filter-out \
	$(KERN_DIR)/arch/x86/% \
	$(KERN_DIR)/apps/user_explorer_entry.asm \
	$(KERN_DIR)/apps/user_fetch.asm \
	$(KERN_DIR)/apps/user_narcpad_entry.asm \
	$(KERN_DIR)/apps/user_netdemo.asm \
	$(KERN_DIR)/apps/user_settings_entry.asm \
	$(KERN_DIR)/apps/user_shell_entry.asm \
	$(KERN_DIR)/apps/user_snake.asm \
	$(KERN_DIR)/apps/user_test.asm, \
	$(X86_64_ALL_ASM_SOURCES))
X86_64_C_OBJECTS = $(patsubst $(KERN_DIR)/%.c,$(X86_64_OBJ_DIR)/%.o,$(X86_64_C_SOURCES))
X86_64_ASM_OBJECTS = $(patsubst $(KERN_DIR)/%.asm,$(X86_64_OBJ_DIR)/%.o,$(X86_64_ASM_SOURCES))
X86_64_USER_PROGRAMS = $(USER_PROGRAMS)
X86_64_USER_PROGRAM_OBJECTS = $(patsubst %,$(X86_64_OBJ_DIR)/user/programs/%.o,$(X86_64_USER_PROGRAMS))
X86_64_USER_BINARIES = $(patsubst %,$(X86_64_OBJ_DIR)/user/bin/%,$(X86_64_USER_PROGRAMS))
X86_64_USER_EMBED_OBJECTS = $(patsubst %,$(X86_64_OBJ_DIR)/user/embed/%.o,$(X86_64_USER_PROGRAMS))
X86_64_USER_CRT_OBJECT = $(X86_64_OBJ_DIR)/user/crt0_x86_64.o
X86_64_KERNEL_OBJECTS = $(X86_64_ASM_OBJECTS) $(X86_64_C_OBJECTS) $(X86_64_USER_EMBED_OBJECTS)
X86_64_KERNEL_ELF = $(X86_64_OBJ_DIR)/kernel64.elf
X86_64_KERNEL_BIN = $(X86_64_OBJ_DIR)/kernel64.bin
X86_64_BOOT_BIN = $(X86_64_OBJ_DIR)/boot/boot.bin
X86_64_STAGE2_BIN = $(X86_64_OBJ_DIR)/boot/stage2.bin
X86_64_IMAGE = $(X86_64_OBJ_DIR)/minios64.img

# Windows (MinGW) 'ld -o raw_binary_file' seklinde tam duz ciktivermeyebilir
# Buna objcopy destek cikar (Fakat biz PE-O yapisindan objcopy cekecegiz)
# Eger minios.img boyutu sacmalarsa LDFLAGS uzerinden --oformat binary zorlanabilir.

.PHONY: all all-i386 all-x86_64 clean export-i386-artifacts pre-build run-i386 run-net run-net-i386 run-x86_64 run-x86_64-gui run-x86_64-headless run-x86_64-net user-programs user-programs-i386 user-programs-x86_64
.SECONDARY: $(I386_USER_BINARIES) $(X86_64_USER_BINARIES) $(X86_64_KERNEL_ELF)

all: all-i386 export-i386-artifacts

all-i386: pre-build $(I386_IMAGE)

all-x86_64: pre-build $(X86_64_IMAGE)

user-programs: user-programs-i386

user-programs-i386: pre-build $(I386_USER_BINARIES)

user-programs-x86_64: pre-build $(X86_64_USER_BINARIES)

pre-build:
	@mkdir -p $(OBJ_DIR)

export-i386-artifacts: $(I386_IMAGE)
	@mkdir -p $(BOOT_DIR)
	cp $(I386_BOOT_BIN) $(BOOT_DIR)/boot.bin
	cp $(I386_STAGE2_BIN) $(BOOT_DIR)/stage2.bin
	cp $(I386_KERNEL_BIN) kernel.bin
	cp $(I386_IMAGE) minios.img

$(I386_BOOT_BIN): $(BOOT_DIR)/boot.asm $(I386_STAGE2_BIN)
	@mkdir -p $(dir $@)
	$(eval STAGE2_SECS := $(shell echo $$(( ($$(wc -c < $(I386_STAGE2_BIN)) + 511) / 512 ))))
	$(AS) -DSTAGE2_SECTORS=$(STAGE2_SECS) -f bin $< -o $@

$(I386_STAGE2_BIN): $(BOOT_DIR)/stage2.asm $(I386_KERNEL_BIN)
	@mkdir -p $(dir $@)
	$(eval KERNEL_SECS := $(shell echo $$(( ($$(wc -c < $(I386_KERNEL_BIN)) + 511) / 512 ))))
	$(AS) -DKERNEL_SECTORS=$(KERNEL_SECS) -f bin $< -o $@

$(I386_OBJ_DIR)/%.o: $(KERN_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(I386_CFLAGS) -c $< -o $@

$(I386_OBJ_DIR)/%.o: $(KERN_DIR)/%.asm
	@mkdir -p $(dir $@)
	$(AS) -f elf32 $< -o $@

$(I386_USER_CRT_OBJECT): $(USER_DIR)/crt0.asm
	@mkdir -p $(dir $@)
	$(AS) -f elf32 $< -o $@

$(I386_OBJ_DIR)/user/programs/%.o: $(USER_DIR)/programs/%.c $(USER_DIR)/include/user_lib.h
	@mkdir -p $(dir $@)
	$(CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_OBJ_DIR)/user/bin/%: $(I386_USER_CRT_OBJECT) $(I386_OBJ_DIR)/user/programs/%.o $(USER_DIR)/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(I386_USER_LDFLAGS) -o $@ $(filter %.o,$^)
	@echo "[OK] i386 user: $@ ($$(wc -c < $@) byte)"

$(I386_OBJ_DIR)/user/embed/%.o: $(I386_OBJ_DIR)/user/bin/%
	@mkdir -p $(dir $@)
	$(LD) -r -b binary -m elf_i386 $< -o $@

$(I386_KERNEL_BIN): $(I386_KERNEL_OBJECTS) linker_i386.ld
	@mkdir -p $(dir $@)
	$(LD) $(I386_LDFLAGS) -o $@ $(I386_KERNEL_OBJECTS)
	@echo "[OK] i386 kernel: $@ ($$(wc -c < $@) byte)"

$(I386_IMAGE): $(I386_BOOT_BIN) $(I386_STAGE2_BIN) $(I386_KERNEL_BIN)
	@mkdir -p $(dir $@)
	$(eval KERNEL_SECS := $(shell echo $$(( ($$(wc -c < $(I386_KERNEL_BIN)) + 511) / 512 + 1 ))))
	@echo "[INFO] i386 kernel sector size: $(KERNEL_SECS)"
	dd if=/dev/zero of=$@ bs=512 count=32768 2>/dev/null
	dd if=$(I386_BOOT_BIN) of=$@ bs=512 seek=0 conv=notrunc 2>/dev/null
	dd if=$(I386_STAGE2_BIN) of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null
	dd if=$(I386_KERNEL_BIN) of=$@ bs=512 seek=18 conv=notrunc 2>/dev/null
	@echo "[OK] i386 image: $@"

$(X86_64_OBJ_DIR)/%.o: $(KERN_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(X86_64_CFLAGS) -c $< -o $@

$(X86_64_OBJ_DIR)/%.o: $(KERN_DIR)/%.asm
	@mkdir -p $(dir $@)
	$(AS) -f elf64 $< -o $@

$(X86_64_USER_CRT_OBJECT): $(USER_DIR)/crt0_x86_64.asm
	@mkdir -p $(dir $@)
	$(AS) -f elf64 $< -o $@

$(X86_64_OBJ_DIR)/user/programs/%.o: $(USER_DIR)/programs/%.c $(USER_DIR)/include/user_lib.h
	@mkdir -p $(dir $@)
	$(CC) $(X86_64_USER_CFLAGS) -c $< -o $@

$(X86_64_OBJ_DIR)/user/bin/%: $(X86_64_USER_CRT_OBJECT) $(X86_64_OBJ_DIR)/user/programs/%.o $(USER_DIR)/linker_x86_64.ld
	@mkdir -p $(dir $@)
	$(LD) -m elf_x86_64 -T $(USER_DIR)/linker_x86_64.ld -nostdlib -s --strip-all -o $@ $(filter %.o,$^)
	@echo "[OK] x86_64 user: $@ ($$(wc -c < $@) byte)"

$(X86_64_OBJ_DIR)/user/embed/%.o: $(X86_64_OBJ_DIR)/user/bin/%
	@mkdir -p $(dir $@)
	$(LD) -r -b binary -m elf_x86_64 $< -o $@

$(X86_64_KERNEL_ELF): $(X86_64_KERNEL_OBJECTS) linker_x86_64.ld
	@mkdir -p $(dir $@)
	$(LD) $(X86_64_LDFLAGS) -o $@ $(X86_64_KERNEL_OBJECTS)
	@echo "[OK] x86_64 experimental kernel: $@ ($$(wc -c < $@) byte)"

$(X86_64_KERNEL_BIN): $(X86_64_KERNEL_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	@echo "[OK] x86_64 raw kernel: $@ ($$(wc -c < $@) byte)"

$(X86_64_STAGE2_BIN): $(BOOT_DIR)/stage2_x86_64.asm $(X86_64_KERNEL_BIN)
	@mkdir -p $(dir $@)
	$(eval KERNEL_SECS := $(shell echo $$(( ($$(wc -c < $(X86_64_KERNEL_BIN)) + 511) / 512 ))))
	$(AS) -DKERNEL_SECTORS=$(KERNEL_SECS) -f bin $< -o $@

$(X86_64_BOOT_BIN): $(BOOT_DIR)/boot.asm $(X86_64_STAGE2_BIN)
	@mkdir -p $(dir $@)
	$(eval STAGE2_SECS := $(shell echo $$(( ($$(wc -c < $(X86_64_STAGE2_BIN)) + 511) / 512 ))))
	$(AS) -DSTAGE2_SECTORS=$(STAGE2_SECS) -f bin $< -o $@

$(X86_64_IMAGE): $(X86_64_BOOT_BIN) $(X86_64_STAGE2_BIN) $(X86_64_KERNEL_BIN)
	@mkdir -p $(dir $@)
	dd if=/dev/zero of=$@ bs=512 count=32768 2>/dev/null
	dd if=$(X86_64_BOOT_BIN) of=$@ bs=512 seek=0 conv=notrunc 2>/dev/null
	dd if=$(X86_64_STAGE2_BIN) of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null
	dd if=$(X86_64_KERNEL_BIN) of=$@ bs=512 seek=18 conv=notrunc 2>/dev/null
	@echo "[OK] x86_64 image: $@"

clean:
	rm -rf $(OBJ_DIR) $(BOOT_DIR)/*.bin *.img kernel.bin kernel64.elf kernel64.bin kernel.tmp

run-i386: all-i386
	qemu-system-i386 -m 128M -drive format=raw,file=$(I386_IMAGE)

run-net-i386: all-i386
	qemu-system-i386 -m 128M -drive format=raw,file=$(I386_IMAGE) -netdev user,id=n0 -device rtl8139,netdev=n0

run-net: run-x86_64-net

run-x86_64: run-x86_64-headless

run-x86_64-headless: all-x86_64
	qemu-system-x86_64 -m 128M -drive format=raw,file=$(X86_64_IMAGE) -serial stdio -display none -no-reboot -no-shutdown

run-x86_64-gui: all-x86_64
	qemu-system-x86_64 -m 128M -drive format=raw,file=$(X86_64_IMAGE) -serial stdio -no-reboot -no-shutdown

run-x86_64-net: all-x86_64
	qemu-system-x86_64 -m 128M -drive format=raw,file=$(X86_64_IMAGE) -serial stdio -netdev user,id=n0 -device rtl8139,netdev=n0 -no-reboot -no-shutdown
