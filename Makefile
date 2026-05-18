# NarcOs Makefile

CC = gcc
LD = ld
AS = nasm
OBJCOPY = objcopy

OBJ_DIR  = obj
BOOT_DIR = boot
KERN_DIR = kernel
USER_DIR = user
ASSET_DIR = assets
VBE_WIDTH ?= 1920
VBE_HEIGHT ?= 1080
BOOT_MANIFEST_LBA = 17
KERNEL_START_LBA = 18
DISK_IMAGE_SECTORS = 49152

KERNEL_DIRS = $(shell find $(KERN_DIR) -type d | sort)
USER_PROGRAMS = hello ps cat echo kill proc_test pipe_test neofetch desktop explorer narcpad settings snake doom core_tools tls_tools
USER_EMBED_PROGRAMS = $(filter-out doom,$(USER_PROGRAMS))
USER_PROGRAM_HEADERS = $(shell find $(USER_DIR)/programs -name '*.h' 2>/dev/null)
DOOM1_WAD = $(wildcard $(ASSET_DIR)/doom1.wad)
DOOM_BIN_LBA = 32768
DOOM_BIN_MAX_SIZE = 1048576
DOOM1_WAD_LBA = 33792
DOOM1_WAD_MAX_SIZE = 4489216
DOOM1_WAD_SIZE = $(if $(DOOM1_WAD),$(shell wc -c < $(ASSET_DIR)/doom1.wad),0)
DOOM1_WAD_CFLAGS = $(if $(DOOM1_WAD),-DNARCOS_DISK_DOOM1_WAD=1 -DNARCOS_DISK_DOOM1_WAD_LBA=$(DOOM1_WAD_LBA) -DNARCOS_DISK_DOOM1_WAD_SIZE=$(DOOM1_WAD_SIZE),)
USER_TLS_PROGRAMS = tls_tools
USER_TLS_SOURCES = \
	$(KERN_DIR)/apps/user_tls.c \
	$(KERN_DIR)/apps/user_tls_crypto.c \
	$(KERN_DIR)/apps/user_tls_bigint.c \
	$(KERN_DIR)/apps/user_tls_x509.c \
	$(KERN_DIR)/apps/user_tls_pins.c
USER_TLS_HEADERS = \
	$(KERN_DIR)/apps/user_tls.h \
	$(KERN_DIR)/apps/user_tls_crypto.h \
	$(KERN_DIR)/apps/user_tls_bigint.h \
	$(KERN_DIR)/apps/user_tls_x509.h \
	$(KERN_DIR)/apps/user_tls_pins.h \
	$(KERN_DIR)/apps/user_string.h
DOOM_PORT_DIR = $(USER_DIR)/ports/doom
DOOMGENERIC_DIR = $(DOOM_PORT_DIR)/doomgeneric
DOOMGENERIC_SOURCE_NAMES = \
	dummy.c am_map.c doomdef.c doomstat.c dstrings.c d_event.c d_items.c d_iwad.c d_loop.c d_main.c d_mode.c d_net.c \
	f_finale.c f_wipe.c g_game.c hu_lib.c hu_stuff.c info.c i_cdmus.c i_endoom.c i_joystick.c i_scale.c i_sound.c \
	i_system.c i_timer.c memio.c m_argv.c m_bbox.c m_cheat.c m_config.c m_controls.c m_fixed.c m_menu.c m_misc.c \
	m_random.c p_ceilng.c p_doors.c p_enemy.c p_floor.c p_inter.c p_lights.c p_map.c p_maputl.c p_mobj.c p_plats.c \
	p_pspr.c p_saveg.c p_setup.c p_sight.c p_spec.c p_switch.c p_telept.c p_tick.c p_user.c r_bsp.c r_data.c \
	r_draw.c r_main.c r_plane.c r_segs.c r_sky.c r_things.c sha1.c sounds.c statdump.c st_lib.c st_stuff.c s_sound.c \
	tables.c v_video.c wi_stuff.c w_checksum.c w_file.c w_main.c w_wad.c z_zone.c w_file_stdc.c i_input.c i_video.c doomgeneric.c
DOOMGENERIC_SOURCES = $(addprefix $(DOOMGENERIC_DIR)/,$(DOOMGENERIC_SOURCE_NAMES))
DOOM_CFLAGS = -I$(DOOM_PORT_DIR)/include -I$(DOOMGENERIC_DIR) -include string.h -include strings.h -DNORMALUNIX -D_DEFAULT_SOURCE -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 -Wno-unused-parameter -Wno-missing-field-initializers -Wno-sign-compare
I386_DOOM_CFLAGS = $(DOOM_CFLAGS)
X86_64_DOOM_CFLAGS = $(DOOM_CFLAGS) -DNARCOS_DOOM_NO_FLOAT

KERNEL_INCLUDE_FLAGS = $(addprefix -I,$(KERNEL_DIRS))
COMMON_CFLAGS = -ffreestanding -fno-pie -fno-pic -fno-stack-protector -fcf-protection=none -fno-builtin -fno-strict-aliasing -Wall -Wextra
COMMON_USER_CFLAGS = -ffreestanding -fno-pie -fno-pic -fno-stack-protector -fcf-protection=none -fno-builtin -fno-strict-aliasing -Wall -Wextra -I$(USER_DIR)/include

I386_OBJ_DIR = $(OBJ_DIR)/i386
I386_CFLAGS = -m32 $(COMMON_CFLAGS) $(KERNEL_INCLUDE_FLAGS) -mpreferred-stack-boundary=2 -mno-red-zone -Os -fomit-frame-pointer
I386_USER_CFLAGS = -m32 $(COMMON_USER_CFLAGS) $(KERNEL_INCLUDE_FLAGS) -mpreferred-stack-boundary=2 -mno-red-zone -O2 -fomit-frame-pointer
I386_LDFLAGS = -m elf_i386 -T linker_i386.ld -nostdlib -s --strip-all
I386_USER_LDFLAGS = -m elf_i386 -T $(USER_DIR)/linker.ld -nostdlib -s --strip-all
I386_C_SOURCES = $(filter-out \
	$(USER_TLS_SOURCES) \
	$(KERN_DIR)/apps/user_explorer.c \
	$(KERN_DIR)/apps/user_narcpad.c \
	$(KERN_DIR)/apps/user_settings.c \
	$(KERN_DIR)/apps/user_snake_app.c, \
	$(shell find $(KERN_DIR) -name '*.c' ! -path '$(KERN_DIR)/arch/x86_64/*' | sort))
I386_ASM_SOURCES = $(filter-out \
	$(KERN_DIR)/apps/user_explorer_entry.asm \
	$(KERN_DIR)/apps/user_narcpad_entry.asm \
	$(KERN_DIR)/apps/user_settings_entry.asm \
	$(KERN_DIR)/apps/user_snake.asm, \
	$(shell find $(KERN_DIR) -name '*.asm' ! -path '$(KERN_DIR)/arch/x86_64/*' | sort))
I386_C_OBJECTS = $(patsubst $(KERN_DIR)/%.c,$(I386_OBJ_DIR)/%.o,$(I386_C_SOURCES))
I386_ASM_OBJECTS = $(patsubst $(KERN_DIR)/%.asm,$(I386_OBJ_DIR)/%.o,$(I386_ASM_SOURCES))
I386_USER_PROGRAM_OBJECTS = $(patsubst %,$(I386_OBJ_DIR)/user/programs/%.o,$(USER_PROGRAMS))
I386_USER_BINARIES = $(patsubst %,$(I386_OBJ_DIR)/user/bin/%,$(USER_PROGRAMS))
I386_USER_EMBED_OBJECTS = $(patsubst %,$(I386_OBJ_DIR)/user/embed/%.o,$(USER_EMBED_PROGRAMS))
I386_USER_TLS_OBJECTS = $(patsubst $(KERN_DIR)/apps/%.c,$(I386_OBJ_DIR)/user/lib/%.o,$(USER_TLS_SOURCES))
I386_USER_TLS_BINARIES = $(patsubst %,$(I386_OBJ_DIR)/user/bin/%,$(USER_TLS_PROGRAMS))
I386_DOOMGENERIC_OBJECTS = $(patsubst $(DOOMGENERIC_DIR)/%.c,$(I386_OBJ_DIR)/doomgeneric/%.o,$(DOOMGENERIC_SOURCES))
I386_LIBGCC = $(shell $(CC) -m32 -print-libgcc-file-name)
I386_ASSET_BG_RGB = $(I386_OBJ_DIR)/assets/bg.rgb
I386_ASSET_BG_OBJECT = $(I386_OBJ_DIR)/assets/bg.o
I386_ASSET_LOGO_RGB = $(I386_OBJ_DIR)/assets/logo.rgb
I386_ASSET_LOGO_OBJECT = $(I386_OBJ_DIR)/assets/logo.o
I386_DESKTOP_ASSET_BG_RGB = $(I386_OBJ_DIR)/user/assets/desktop_bg.rgb
I386_DESKTOP_ASSET_BG_OBJECT = $(I386_OBJ_DIR)/user/assets/desktop_bg.o
I386_USER_CRT_OBJECT = $(I386_OBJ_DIR)/user/crt0.o
I386_DOOM_BINARY = $(I386_OBJ_DIR)/user/bin/doom
I386_DOOM_BIN_SIZE = $(shell test -f $(I386_DOOM_BINARY) && wc -c < $(I386_DOOM_BINARY) || echo 0)
I386_DISK_PAYLOAD_CFLAGS = -DNARCOS_DISK_DOOM_BIN=1 -DNARCOS_DISK_DOOM_BIN_LBA=$(DOOM_BIN_LBA) -DNARCOS_DISK_DOOM_BIN_SIZE=$(I386_DOOM_BIN_SIZE) $(DOOM1_WAD_CFLAGS)
I386_KERNEL_OBJECTS = $(I386_ASM_OBJECTS) $(I386_C_OBJECTS) $(I386_USER_EMBED_OBJECTS) $(I386_ASSET_BG_OBJECT) $(I386_ASSET_LOGO_OBJECT)
I386_BOOT_BIN = $(I386_OBJ_DIR)/boot/boot.bin
I386_STAGE2_BIN = $(I386_OBJ_DIR)/boot/stage2.bin
I386_BOOT_MANIFEST_BIN = $(I386_OBJ_DIR)/boot/manifest.bin
I386_KERNEL_ELF = $(I386_OBJ_DIR)/kernel.elf
I386_KERNEL_BIN = $(I386_OBJ_DIR)/kernel.bin
I386_IMAGE = $(I386_OBJ_DIR)/minios.img

X86_64_OBJ_DIR = $(OBJ_DIR)/x86_64
X86_64_CFLAGS = -m64 $(COMMON_CFLAGS) -I$(KERN_DIR)/arch/x86_64 $(KERNEL_INCLUDE_FLAGS) -mno-red-zone -mgeneral-regs-only -mno-mmx -mno-sse -mno-sse2 -msoft-float -O2 -fomit-frame-pointer
X86_64_USER_CFLAGS = -m64 $(COMMON_USER_CFLAGS) -I$(KERN_DIR)/arch/x86_64 $(KERNEL_INCLUDE_FLAGS) -mno-red-zone -mgeneral-regs-only -mno-mmx -mno-sse -mno-sse2 -msoft-float -O2 -fomit-frame-pointer
X86_64_LDFLAGS = -m elf_x86_64 -T linker_x86_64.ld -nostdlib
X86_64_ALL_C_SOURCES = $(filter-out $(USER_TLS_SOURCES),$(shell find $(KERN_DIR) -name '*.c' | sort))
X86_64_C_SOURCES = $(filter-out \
	$(KERN_DIR)/arch/x86/% \
	$(KERN_DIR)/arch/x86_64/display.c \
	$(KERN_DIR)/arch/x86_64/main.c \
	$(KERN_DIR)/arch/x86_64/stub.c \
	$(KERN_DIR)/arch/x86_64/stubs.c \
	$(KERN_DIR)/arch/x86_64/user_snake.c \
	$(KERN_DIR)/arch/x86_64/usermode.c \
	$(KERN_DIR)/apps/user_explorer.c \
	$(KERN_DIR)/apps/user_narcpad.c \
	$(KERN_DIR)/apps/user_settings.c \
	$(KERN_DIR)/apps/user_snake_app.c \
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
X86_64_USER_EMBED_OBJECTS = $(patsubst %,$(X86_64_OBJ_DIR)/user/embed/%.o,$(USER_EMBED_PROGRAMS))
X86_64_USER_TLS_OBJECTS = $(patsubst $(KERN_DIR)/apps/%.c,$(X86_64_OBJ_DIR)/user/lib/%.o,$(USER_TLS_SOURCES))
X86_64_USER_TLS_BINARIES = $(patsubst %,$(X86_64_OBJ_DIR)/user/bin/%,$(USER_TLS_PROGRAMS))
X86_64_DOOMGENERIC_OBJECTS = $(patsubst $(DOOMGENERIC_DIR)/%.c,$(X86_64_OBJ_DIR)/doomgeneric/%.o,$(DOOMGENERIC_SOURCES))
X86_64_LIBGCC = $(shell $(CC) -m64 -print-libgcc-file-name)
X86_64_ASSET_BG_RGB = $(X86_64_OBJ_DIR)/assets/bg.rgb
X86_64_ASSET_BG_OBJECT = $(X86_64_OBJ_DIR)/assets/bg.o
X86_64_ASSET_LOGO_RGB = $(X86_64_OBJ_DIR)/assets/logo.rgb
X86_64_ASSET_LOGO_OBJECT = $(X86_64_OBJ_DIR)/assets/logo.o
X86_64_DESKTOP_ASSET_BG_RGB = $(X86_64_OBJ_DIR)/user/assets/desktop_bg.rgb
X86_64_DESKTOP_ASSET_BG_OBJECT = $(X86_64_OBJ_DIR)/user/assets/desktop_bg.o
X86_64_USER_CRT_OBJECT = $(X86_64_OBJ_DIR)/user/crt0_x86_64.o
X86_64_DOOM_BINARY = $(X86_64_OBJ_DIR)/user/bin/doom
X86_64_DOOM_BIN_SIZE = $(shell test -f $(X86_64_DOOM_BINARY) && wc -c < $(X86_64_DOOM_BINARY) || echo 0)
X86_64_DISK_PAYLOAD_CFLAGS = -DNARCOS_DISK_DOOM_BIN=1 -DNARCOS_DISK_DOOM_BIN_LBA=$(DOOM_BIN_LBA) -DNARCOS_DISK_DOOM_BIN_SIZE=$(X86_64_DOOM_BIN_SIZE) $(DOOM1_WAD_CFLAGS)
X86_64_KERNEL_OBJECTS = $(X86_64_ASM_OBJECTS) $(X86_64_C_OBJECTS) $(X86_64_USER_EMBED_OBJECTS) $(X86_64_ASSET_BG_OBJECT) $(X86_64_ASSET_LOGO_OBJECT)
X86_64_KERNEL_ELF = $(X86_64_OBJ_DIR)/kernel64.elf
X86_64_KERNEL_BIN = $(X86_64_OBJ_DIR)/kernel64.bin
X86_64_BOOT_BIN = $(X86_64_OBJ_DIR)/boot/boot.bin
X86_64_STAGE2_BIN = $(X86_64_OBJ_DIR)/boot/stage2.bin
X86_64_BOOT_MANIFEST_BIN = $(X86_64_OBJ_DIR)/boot/manifest.bin
X86_64_IMAGE = $(X86_64_OBJ_DIR)/minios64.img

# Windows (MinGW) 'ld -o raw_binary_file' seklinde tam duz ciktivermeyebilir
# Buna objcopy destek cikar (Fakat biz PE-O yapisindan objcopy cekecegiz)
# Eger minios.img boyutu sacmalarsa LDFLAGS uzerinden --oformat binary zorlanabilir.

.PHONY: all all-i386 all-x86_64 clean export-i386-artifacts pre-build run-i386 run-net run-net-i386 run-x86_64 run-x86_64-gui run-x86_64-headless run-x86_64-net user-programs user-programs-i386 user-programs-x86_64
.SECONDARY: $(I386_USER_BINARIES) $(X86_64_USER_BINARIES) $(I386_KERNEL_ELF) $(X86_64_KERNEL_ELF)

all: all-i386 export-i386-artifacts

all-i386: pre-build $(I386_IMAGE)

all-x86_64: pre-build $(X86_64_IMAGE)

user-programs: user-programs-i386

user-programs-i386: pre-build $(I386_USER_BINARIES)

user-programs-x86_64: pre-build $(X86_64_USER_BINARIES)

pre-build:
	@mkdir -p $(OBJ_DIR)

export-i386-artifacts: $(I386_IMAGE) $(I386_KERNEL_BIN)
	@mkdir -p $(BOOT_DIR)
	cp $(I386_BOOT_BIN) $(BOOT_DIR)/boot.bin
	cp $(I386_STAGE2_BIN) $(BOOT_DIR)/stage2.bin
	cp $(I386_BOOT_MANIFEST_BIN) $(BOOT_DIR)/manifest.bin
	cp $(I386_KERNEL_BIN) kernel.bin
	cp $(I386_KERNEL_ELF) kernel.elf
	cp $(I386_IMAGE) minios.img

$(I386_BOOT_BIN): $(BOOT_DIR)/boot.asm $(I386_STAGE2_BIN)
	@mkdir -p $(dir $@)
	$(eval STAGE2_SECS := $(shell echo $$(( ($$(wc -c < $(I386_STAGE2_BIN)) + 511) / 512 ))))
	@test $(STAGE2_SECS) -le 16 || (echo "[ERR] i386 stage2 too large for manifest layout: $(STAGE2_SECS) sectors > 16" && exit 1)
	$(AS) -DSTAGE2_SECTORS=$(STAGE2_SECS) -f bin $< -o $@

$(I386_STAGE2_BIN): $(BOOT_DIR)/stage2.asm $(I386_KERNEL_ELF)
	@mkdir -p $(dir $@)
	$(eval KERNEL_SECS := $(shell echo $$(( ($$(wc -c < $(I386_KERNEL_ELF)) + 511) / 512 ))))
	$(AS) -DKERNEL_SECTORS=$(KERNEL_SECS) -DBOOT_MANIFEST_LBA=$(BOOT_MANIFEST_LBA) -DVBE_WIDTH=$(VBE_WIDTH) -DVBE_HEIGHT=$(VBE_HEIGHT) -f bin $< -o $@

$(I386_OBJ_DIR)/%.o: $(KERN_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(I386_CFLAGS) -c $< -o $@

$(I386_OBJ_DIR)/fs/fs.o: $(I386_DOOM_BINARY) $(DOOM1_WAD)
$(I386_OBJ_DIR)/fs/fs.o: I386_CFLAGS += $(I386_DISK_PAYLOAD_CFLAGS)

$(I386_OBJ_DIR)/%.o: $(KERN_DIR)/%.asm
	@mkdir -p $(dir $@)
	$(AS) -f elf32 $< -o $@

$(I386_USER_CRT_OBJECT): $(USER_DIR)/crt0.asm
	@mkdir -p $(dir $@)
	$(AS) -f elf32 $< -o $@

$(I386_OBJ_DIR)/user/programs/%.o: $(USER_DIR)/programs/%.c $(USER_DIR)/include/user_lib.h $(USER_PROGRAM_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_OBJ_DIR)/user/programs/doom.o: $(USER_DIR)/programs/doom.c $(USER_DIR)/include/user_lib.h $(USER_PROGRAM_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(CC) $(I386_DOOM_CFLAGS) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_OBJ_DIR)/user/programs/doom_libc.o: $(USER_DIR)/programs/doom_libc.c $(USER_DIR)/include/user_lib.h $(USER_PROGRAM_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(CC) $(I386_DOOM_CFLAGS) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_OBJ_DIR)/doomgeneric/%.o: $(DOOMGENERIC_DIR)/%.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(I386_DOOM_CFLAGS) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_OBJ_DIR)/user/lib/%.o: $(KERN_DIR)/apps/%.c $(USER_TLS_HEADERS) $(USER_DIR)/include/user_lib.h
	@mkdir -p $(dir $@)
	$(CC) $(I386_USER_CFLAGS) -c $< -o $@

$(I386_OBJ_DIR)/user/bin/%: $(I386_USER_CRT_OBJECT) $(I386_OBJ_DIR)/user/programs/%.o $(USER_DIR)/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(I386_USER_LDFLAGS) -o $@ $(filter %.o,$^)
	@echo "[OK] i386 user: $@ ($$(wc -c < $@) byte)"

$(I386_OBJ_DIR)/user/bin/doom: $(I386_USER_CRT_OBJECT) $(I386_OBJ_DIR)/user/programs/doom.o $(I386_OBJ_DIR)/user/programs/doom_libc.o $(I386_DOOMGENERIC_OBJECTS) $(USER_DIR)/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(I386_USER_LDFLAGS) -o $@ $(filter %.o,$^) $(I386_LIBGCC)
	@echo "[OK] i386 user: $@ ($$(wc -c < $@) byte)"

$(I386_USER_TLS_BINARIES): $(I386_OBJ_DIR)/user/bin/%: $(I386_USER_CRT_OBJECT) $(I386_OBJ_DIR)/user/programs/%.o $(I386_USER_TLS_OBJECTS) $(USER_DIR)/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(I386_USER_LDFLAGS) -o $@ $(filter %.o,$^)
	@echo "[OK] i386 user: $@ ($$(wc -c < $@) byte)"

$(I386_OBJ_DIR)/user/bin/desktop: $(I386_USER_CRT_OBJECT) $(I386_OBJ_DIR)/user/programs/desktop.o $(I386_DESKTOP_ASSET_BG_OBJECT) $(USER_DIR)/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(I386_USER_LDFLAGS) -o $@ $(filter %.o,$^)
	@echo "[OK] i386 user: $@ ($$(wc -c < $@) byte)"

$(I386_OBJ_DIR)/user/embed/%.o: $(I386_OBJ_DIR)/user/bin/%
	@mkdir -p $(dir $@)
	$(LD) -r -b binary -m elf_i386 $< -o $@

$(I386_ASSET_BG_RGB): $(ASSET_DIR)/bg.png Makefile
	@mkdir -p $(dir $@)
	magick $< -resize 96x54^ -gravity center -extent 96x54 -alpha off -depth 8 rgb:$@

$(I386_ASSET_BG_OBJECT): $(I386_ASSET_BG_RGB)
	@mkdir -p $(dir $@)
	$(LD) -r -b binary -m elf_i386 $< -o $@

$(I386_ASSET_LOGO_RGB): $(ASSET_DIR)/logo.png
	@mkdir -p $(dir $@)
	magick $< -background "#0B1016" -alpha remove -alpha off -resize 24x24^ -gravity center -extent 24x24 -depth 8 rgb:$@

$(I386_ASSET_LOGO_OBJECT): $(I386_ASSET_LOGO_RGB)
	@mkdir -p $(dir $@)
	$(LD) -r -b binary -m elf_i386 $< -o $@

$(I386_DESKTOP_ASSET_BG_RGB): $(ASSET_DIR)/bg.png Makefile
	@mkdir -p $(dir $@)
	magick $< -filter Lanczos -resize 320x180^ -gravity center -extent 320x180 -alpha off -depth 8 rgb:$@

$(I386_DESKTOP_ASSET_BG_OBJECT): $(I386_DESKTOP_ASSET_BG_RGB)
	@mkdir -p $(dir $@)
	$(LD) -r -b binary -m elf_i386 $< -o $@

$(I386_KERNEL_ELF): $(I386_KERNEL_OBJECTS) linker_i386.ld
	@mkdir -p $(dir $@)
	$(LD) $(I386_LDFLAGS) -o $@ $(I386_KERNEL_OBJECTS)
	@echo "[OK] i386 kernel ELF: $@ ($$(wc -c < $@) byte)"

$(I386_KERNEL_BIN): $(I386_KERNEL_ELF)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -O binary $< $@
	@echo "[OK] i386 raw kernel: $@ ($$(wc -c < $@) byte)"

$(I386_BOOT_MANIFEST_BIN): $(I386_KERNEL_ELF)
	@mkdir -p $(dir $@)
	python3 -c 'import struct, sys, zlib; src, dst, lba = sys.argv[1], sys.argv[2], int(sys.argv[3]); data = open(src, "rb").read(); sectors = (len(data) + 511) // 512; buf = bytearray(512); struct.pack_into("<IHHIIIIIIIII", buf, 0, 0x4D43524E, 1, 64, 0, lba, sectors, len(data), zlib.crc32(data) & 0xFFFFFFFF, 0, 0, 0, 0); open(dst, "wb").write(buf)' $< $@ $(KERNEL_START_LBA)
	@echo "[OK] i386 boot manifest: $@"

$(I386_IMAGE): $(I386_BOOT_BIN) $(I386_STAGE2_BIN) $(I386_BOOT_MANIFEST_BIN) $(I386_KERNEL_ELF) $(I386_DOOM_BINARY)
	@mkdir -p $(dir $@)
	$(eval KERNEL_SECS := $(shell echo $$(( ($$(wc -c < $(I386_KERNEL_ELF)) + 511) / 512 ))))
	@echo "[INFO] i386 kernel sector size: $(KERNEL_SECS)"
	dd if=/dev/zero of=$@ bs=512 count=$(DISK_IMAGE_SECTORS) 2>/dev/null
	dd if=$(I386_BOOT_BIN) of=$@ bs=512 seek=0 conv=notrunc 2>/dev/null
	dd if=$(I386_STAGE2_BIN) of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null
	dd if=$(I386_BOOT_MANIFEST_BIN) of=$@ bs=512 seek=$(BOOT_MANIFEST_LBA) conv=notrunc 2>/dev/null
	dd if=$(I386_KERNEL_ELF) of=$@ bs=512 seek=$(KERNEL_START_LBA) conv=notrunc 2>/dev/null
	@test $(I386_DOOM_BIN_SIZE) -le $(DOOM_BIN_MAX_SIZE) || (echo "[ERR] i386 doom binary too large for payload slot: $(I386_DOOM_BIN_SIZE) > $(DOOM_BIN_MAX_SIZE)" && exit 1)
	dd if=$(I386_DOOM_BINARY) of=$@ bs=512 seek=$(DOOM_BIN_LBA) conv=notrunc 2>/dev/null
	$(if $(DOOM1_WAD),@test $(DOOM1_WAD_SIZE) -le $(DOOM1_WAD_MAX_SIZE) || (echo "[ERR] assets/doom1.wad too large for payload slot: $(DOOM1_WAD_SIZE) > $(DOOM1_WAD_MAX_SIZE)" && exit 1),)
	$(if $(DOOM1_WAD),dd if=$(DOOM1_WAD) of=$@ bs=512 seek=$(DOOM1_WAD_LBA) conv=notrunc 2>/dev/null,)
	@echo "[OK] i386 image: $@"

$(X86_64_OBJ_DIR)/%.o: $(KERN_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(X86_64_CFLAGS) -c $< -o $@

$(X86_64_OBJ_DIR)/fs/fs.o: $(X86_64_DOOM_BINARY) $(DOOM1_WAD)
$(X86_64_OBJ_DIR)/fs/fs.o: X86_64_CFLAGS += $(X86_64_DISK_PAYLOAD_CFLAGS)

$(X86_64_OBJ_DIR)/%.o: $(KERN_DIR)/%.asm
	@mkdir -p $(dir $@)
	$(AS) -f elf64 $< -o $@

$(X86_64_USER_CRT_OBJECT): $(USER_DIR)/crt0_x86_64.asm
	@mkdir -p $(dir $@)
	$(AS) -f elf64 $< -o $@

$(X86_64_OBJ_DIR)/user/programs/%.o: $(USER_DIR)/programs/%.c $(USER_DIR)/include/user_lib.h $(USER_PROGRAM_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(X86_64_USER_CFLAGS) -c $< -o $@

$(X86_64_OBJ_DIR)/user/programs/doom.o: $(USER_DIR)/programs/doom.c $(USER_DIR)/include/user_lib.h $(USER_PROGRAM_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(CC) $(X86_64_DOOM_CFLAGS) $(X86_64_USER_CFLAGS) -c $< -o $@

$(X86_64_OBJ_DIR)/user/programs/doom_libc.o: $(USER_DIR)/programs/doom_libc.c $(USER_DIR)/include/user_lib.h $(USER_PROGRAM_HEADERS) Makefile
	@mkdir -p $(dir $@)
	$(CC) $(X86_64_DOOM_CFLAGS) $(X86_64_USER_CFLAGS) -c $< -o $@

$(X86_64_OBJ_DIR)/doomgeneric/%.o: $(DOOMGENERIC_DIR)/%.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(X86_64_DOOM_CFLAGS) $(X86_64_USER_CFLAGS) -c $< -o $@

$(X86_64_OBJ_DIR)/user/lib/%.o: $(KERN_DIR)/apps/%.c $(USER_TLS_HEADERS) $(USER_DIR)/include/user_lib.h
	@mkdir -p $(dir $@)
	$(CC) $(X86_64_USER_CFLAGS) -c $< -o $@

$(X86_64_OBJ_DIR)/user/bin/%: $(X86_64_USER_CRT_OBJECT) $(X86_64_OBJ_DIR)/user/programs/%.o $(USER_DIR)/linker_x86_64.ld
	@mkdir -p $(dir $@)
	$(LD) -m elf_x86_64 -T $(USER_DIR)/linker_x86_64.ld -nostdlib -s --strip-all -o $@ $(filter %.o,$^)
	@echo "[OK] x86_64 user: $@ ($$(wc -c < $@) byte)"

$(X86_64_OBJ_DIR)/user/bin/doom: $(X86_64_USER_CRT_OBJECT) $(X86_64_OBJ_DIR)/user/programs/doom.o $(X86_64_OBJ_DIR)/user/programs/doom_libc.o $(X86_64_DOOMGENERIC_OBJECTS) $(USER_DIR)/linker_x86_64.ld
	@mkdir -p $(dir $@)
	$(LD) -m elf_x86_64 -T $(USER_DIR)/linker_x86_64.ld -nostdlib -s --strip-all -o $@ $(filter %.o,$^) $(X86_64_LIBGCC)
	@echo "[OK] x86_64 user: $@ ($$(wc -c < $@) byte)"

$(X86_64_USER_TLS_BINARIES): $(X86_64_OBJ_DIR)/user/bin/%: $(X86_64_USER_CRT_OBJECT) $(X86_64_OBJ_DIR)/user/programs/%.o $(X86_64_USER_TLS_OBJECTS) $(USER_DIR)/linker_x86_64.ld
	@mkdir -p $(dir $@)
	$(LD) -m elf_x86_64 -T $(USER_DIR)/linker_x86_64.ld -nostdlib -s --strip-all -o $@ $(filter %.o,$^)
	@echo "[OK] x86_64 user: $@ ($$(wc -c < $@) byte)"

$(X86_64_OBJ_DIR)/user/bin/desktop: $(X86_64_USER_CRT_OBJECT) $(X86_64_OBJ_DIR)/user/programs/desktop.o $(X86_64_DESKTOP_ASSET_BG_OBJECT) $(USER_DIR)/linker_x86_64.ld
	@mkdir -p $(dir $@)
	$(LD) -m elf_x86_64 -T $(USER_DIR)/linker_x86_64.ld -nostdlib -s --strip-all -o $@ $(filter %.o,$^)
	@echo "[OK] x86_64 user: $@ ($$(wc -c < $@) byte)"

$(X86_64_OBJ_DIR)/user/embed/%.o: $(X86_64_OBJ_DIR)/user/bin/%
	@mkdir -p $(dir $@)
	$(LD) -r -b binary -m elf_x86_64 $< -o $@

$(X86_64_ASSET_BG_RGB): $(ASSET_DIR)/bg.png Makefile
	@mkdir -p $(dir $@)
	magick $< -resize 160x90^ -gravity center -extent 160x90 -alpha off -depth 8 rgb:$@

$(X86_64_ASSET_BG_OBJECT): $(X86_64_ASSET_BG_RGB)
	@mkdir -p $(dir $@)
	$(LD) -r -b binary -m elf_x86_64 $< -o $@

$(X86_64_ASSET_LOGO_RGB): $(ASSET_DIR)/logo.png
	@mkdir -p $(dir $@)
	magick $< -background "#0B1016" -alpha remove -alpha off -resize 24x24^ -gravity center -extent 24x24 -depth 8 rgb:$@

$(X86_64_ASSET_LOGO_OBJECT): $(X86_64_ASSET_LOGO_RGB)
	@mkdir -p $(dir $@)
	$(LD) -r -b binary -m elf_x86_64 $< -o $@

$(X86_64_DESKTOP_ASSET_BG_RGB): $(ASSET_DIR)/bg.png Makefile
	@mkdir -p $(dir $@)
	magick $< -filter Lanczos -resize 224x126^ -gravity center -extent 224x126 -alpha off -depth 8 rgb:$@

$(X86_64_DESKTOP_ASSET_BG_OBJECT): $(X86_64_DESKTOP_ASSET_BG_RGB)
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

$(X86_64_STAGE2_BIN): $(BOOT_DIR)/stage2_x86_64.asm $(X86_64_KERNEL_ELF)
	@mkdir -p $(dir $@)
	$(eval KERNEL_SECS := $(shell echo $$(( ($$(wc -c < $(X86_64_KERNEL_ELF)) + 511) / 512 ))))
	$(AS) -DKERNEL_SECTORS=$(KERNEL_SECS) -DBOOT_MANIFEST_LBA=$(BOOT_MANIFEST_LBA) -DVBE_WIDTH=$(VBE_WIDTH) -DVBE_HEIGHT=$(VBE_HEIGHT) -f bin $< -o $@

$(X86_64_BOOT_BIN): $(BOOT_DIR)/boot.asm $(X86_64_STAGE2_BIN)
	@mkdir -p $(dir $@)
	$(eval STAGE2_SECS := $(shell echo $$(( ($$(wc -c < $(X86_64_STAGE2_BIN)) + 511) / 512 ))))
	@test $(STAGE2_SECS) -le 16 || (echo "[ERR] x86_64 stage2 too large for manifest layout: $(STAGE2_SECS) sectors > 16" && exit 1)
	$(AS) -DSTAGE2_SECTORS=$(STAGE2_SECS) -f bin $< -o $@

$(X86_64_BOOT_MANIFEST_BIN): $(X86_64_KERNEL_ELF)
	@mkdir -p $(dir $@)
	python3 -c 'import struct, sys, zlib; src, dst, lba = sys.argv[1], sys.argv[2], int(sys.argv[3]); data = open(src, "rb").read(); sectors = (len(data) + 511) // 512; buf = bytearray(512); struct.pack_into("<IHHIIIIIIIII", buf, 0, 0x4D43524E, 1, 64, 0, lba, sectors, len(data), zlib.crc32(data) & 0xFFFFFFFF, 0, 0, 0, 0); open(dst, "wb").write(buf)' $< $@ $(KERNEL_START_LBA)
	@echo "[OK] x86_64 boot manifest: $@"

$(X86_64_IMAGE): $(X86_64_BOOT_BIN) $(X86_64_STAGE2_BIN) $(X86_64_BOOT_MANIFEST_BIN) $(X86_64_KERNEL_ELF) $(X86_64_DOOM_BINARY)
	@mkdir -p $(dir $@)
	dd if=/dev/zero of=$@ bs=512 count=$(DISK_IMAGE_SECTORS) 2>/dev/null
	dd if=$(X86_64_BOOT_BIN) of=$@ bs=512 seek=0 conv=notrunc 2>/dev/null
	dd if=$(X86_64_STAGE2_BIN) of=$@ bs=512 seek=1 conv=notrunc 2>/dev/null
	dd if=$(X86_64_BOOT_MANIFEST_BIN) of=$@ bs=512 seek=$(BOOT_MANIFEST_LBA) conv=notrunc 2>/dev/null
	dd if=$(X86_64_KERNEL_ELF) of=$@ bs=512 seek=$(KERNEL_START_LBA) conv=notrunc 2>/dev/null
	@test $(X86_64_DOOM_BIN_SIZE) -le $(DOOM_BIN_MAX_SIZE) || (echo "[ERR] x86_64 doom binary too large for payload slot: $(X86_64_DOOM_BIN_SIZE) > $(DOOM_BIN_MAX_SIZE)" && exit 1)
	dd if=$(X86_64_DOOM_BINARY) of=$@ bs=512 seek=$(DOOM_BIN_LBA) conv=notrunc 2>/dev/null
	$(if $(DOOM1_WAD),@test $(DOOM1_WAD_SIZE) -le $(DOOM1_WAD_MAX_SIZE) || (echo "[ERR] assets/doom1.wad too large for payload slot: $(DOOM1_WAD_SIZE) > $(DOOM1_WAD_MAX_SIZE)" && exit 1),)
	$(if $(DOOM1_WAD),dd if=$(DOOM1_WAD) of=$@ bs=512 seek=$(DOOM1_WAD_LBA) conv=notrunc 2>/dev/null,)
	@echo "[OK] x86_64 image: $@"

clean:
	rm -rf $(OBJ_DIR) $(BOOT_DIR)/*.bin *.img kernel.bin kernel.elf kernel64.elf kernel64.bin kernel.tmp

run-i386: all-i386
	qemu-system-i386 -m 128M -drive format=raw,file=$(I386_IMAGE) -serial stdio -no-reboot -no-shutdown

run-net-i386: all-i386
	qemu-system-i386 -m 128M -drive format=raw,file=$(I386_IMAGE) -serial stdio -netdev user,id=n0 -device rtl8139,netdev=n0 -no-reboot -no-shutdown

run-net: run-x86_64-net

run-x86_64: run-x86_64-headless

run-x86_64-headless: all-x86_64
	qemu-system-x86_64 -m 128M -drive format=raw,file=$(X86_64_IMAGE) -serial stdio -display none -no-reboot -no-shutdown

run-x86_64-gui: all-x86_64
	qemu-system-x86_64 -m 128M -drive format=raw,file=$(X86_64_IMAGE) -serial stdio -no-reboot -no-shutdown

run-x86_64-net: all-x86_64
	qemu-system-x86_64 -m 128M -drive format=raw,file=$(X86_64_IMAGE) -serial stdio -netdev user,id=n0 -device rtl8139,netdev=n0 -no-reboot -no-shutdown
