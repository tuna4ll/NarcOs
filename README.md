# NarcOs

A small 32-bit x86 hobby operating system built from scratch.

NarcOs boots through a BIOS stage1/stage2 loader, switches into protected mode, brings up paging, and runs a compact desktop-style environment with its own storage, process, graphics, and networking layers.

![NarcOs desktop](https://cdn.discordapp.com/attachments/1038164986217893920/1487600519177310470/image.png?ex=69c9bb72&is=69c869f2&hm=fe4ab70f7f492ddf4d9f296d434b9104b60a1adfefec4e356149bcbd6bac8615&)

## Highlights

- BIOS MBR bootloader with stage2 loader
- 32-bit protected mode kernel with GDT, IDT, paging, syscalls, and timer-driven scheduling
- VBE graphics path with a desktop shell, windows, terminal, explorer, editor, and snake
- ATA PIO and AHCI storage support with a custom filesystem
- RTL8139 networking with ARP, IPv4, UDP, ICMP, DHCP, DNS, basic TCP, HTTP, and NTP
- VGA text fallback when framebuffer init is unavailable

## Build

Requirements:

- `gcc` with 32-bit output support
- `ld`
- `nasm`
- `qemu-system-i386`

Build the image:

```bash
make all
```

Build only the packaged user executables:

```bash
make user-programs
```

This produces:

- `kernel.bin`
- `boot/boot.bin`
- `boot/stage2.bin`
- `minios.img`
- `obj/user/bin/{hello,ps,cat,echo,kill}`

The user executables are embedded into the kernel image at build time and synced into `/bin` when the NarcOs filesystem initializes.

Clean generated files:

```bash
make clean
```

## Run

Basic QEMU boot:

```bash
qemu-system-i386 -m 128M -drive format=raw,file=minios.img
```

Network-enabled QEMU boot:

```bash
make run-net
```

## Status

NarcOs is experimental and actively changing. BIOS boot, GUI, custom storage, and basic networking are working in QEMU. Real hardware coverage, UEFI boot, and broader driver support are still in progress.

For the current target list and remaining work, see `roadmap.txt`.
