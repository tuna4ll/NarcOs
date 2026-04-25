# NarcOs

NarcOs is a hobby operating system for `x86` and `x86_64`.
It boots from BIOS, has its own bootloader, scheduler, filesystem, GUI, and basic network stack.

## Requirements

- `gcc` with 32-bit output support
- `ld`
- `nasm`
- `objcopy`
- `qemu-system-i386`
- `qemu-system-x86_64`

## Build

Default i386 build with exported compatibility artifacts:

```bash
make all
```

Architecture-specific builds:

```bash
make all-i386
make all-x86_64
```

Only build packaged user programs:

```bash
make user-programs
make user-programs-x86_64
```

Clean generated files:

```bash
make clean
```

## Run

```bash
make run-i386
make run-net-i386
make run-x86_64
make run-x86_64-gui
make run-x86_64-net
```

`make run-net` is an alias for `make run-x86_64-net`.

## Output

Main build artifacts are written under:

- `obj/i386/`
- `obj/x86_64/`

The default `make all` target also exports:

- `kernel.bin`
- `boot/boot.bin`
- `boot/stage2.bin`
- `minios.img`

## Userspace

- `i386` kernel builds and runs `ELF32` userspace
- `x86_64` kernel builds and runs `ELF64` userspace
