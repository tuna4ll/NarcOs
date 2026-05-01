<img width="842" height="245" alt="narcos-final_main" src="https://github.com/user-attachments/assets/4102a490-aec2-40c0-9429-bd1ede27c000" />

# NarcOs

A simple hobby operating system. It can run as `i386` or `x86_64`.

## Requirements

- `gcc`
- `ld`
- `nasm`
- `objcopy`
- `qemu-system-i386`
- `qemu-system-x86_64`

## Run 32-bit

```bash
make run-i386
```

Run with networking:

```bash
make run-net-i386
```

## Run 64-bit

```bash
make run-x86_64
```

Run with networking:

```bash
make run-x86_64-net
```

## Clean

```bash
make clean
```
