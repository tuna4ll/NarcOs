all: minios.img

boot.bin: boot.asm
	nasm -f bin boot.asm -o boot.bin
	@echo "[OK] Stage1: boot.bin ($$(wc -c < boot.bin) byte)"

stage2.bin: stage2.asm
	nasm -f bin stage2.asm -o stage2.bin
	@echo "[OK] Stage2: stage2.bin ($$(wc -c < stage2.bin) byte)"

kernel.bin: kernel.asm
	nasm -f bin kernel.asm -o kernel.bin
	@echo "[OK] Kernel: kernel.bin ($$(wc -c < kernel.bin) byte)"

# Disk imajı oluştur (dd ile birleştir)
minios.img: boot.bin stage2.bin kernel.bin
	dd if=/dev/zero   of=minios.img bs=512 count=50 2>/dev/null
	dd if=boot.bin    of=minios.img bs=512 seek=0  conv=notrunc 2>/dev/null
	dd if=stage2.bin  of=minios.img bs=512 seek=1  conv=notrunc 2>/dev/null
	dd if=kernel.bin  of=minios.img bs=512 seek=17 conv=notrunc 2>/dev/null
	@echo "[OK] minios.img hazir ($$(wc -c < minios.img) byte)"

run: minios.img
	qemu-system-x86_64 -drive format=raw,file=minios.img -m 64M

clean:
	rm -f *.bin *.img

.PHONY: all run clean
