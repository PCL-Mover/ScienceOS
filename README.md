# TangPingOS

TangPingOS is a tiny x86_64 operating system project booted through UEFI with
Limine. It is built one verified stage at a time: kernel bring-up, interrupts,
memory management, scheduling, user mode, VFS/initrd, shell, and standalone user
programs.

## Current Status

- Boots on x86_64 UEFI through Limine.
- Prints to the framebuffer and serial console.
- Has GDT/IDT, exception reporting, PIC/PIT IRQs, PS/2 keyboard input, and a
  preemptive round-robin scheduler.
- Runs Ring 3 ELF user programs with per-process address spaces.
- Loads `init.elf` as the first process, then starts `/bin/shell.elf` from
  initrd.
- Provides devfs, initrd, writable ramfs, file descriptors, `dup2`, `lseek`,
  `unlink`, `spawn`, path-based VFS mount dispatch, mount-table queries, and
  basic system information syscalls.
- Provides a block-device layer with in-memory `ramblk0`, QEMU virtio-blk
  `vd0`, MBR/GPT partition devices such as `vd0p1` and `vd0p2`, an exFAT
  test mount at `/usb` with guarded write-path validation builds, a USB
  Mass Storage exFAT test mount at `/usbdisk` in QEMU with whitelisted
  uppercase 8.3 root-file writes, readonly FAT32 boot-partition samples
  mounted at `/boot` and `/usbboot`, and conservative readonly probing for
  non-QEMU xHCI USB Mass Storage devices with exFAT write-readiness audit
  plus main/backup boot-region checksum and dry-run write-sector reporting
  exposed through `/usbdisk/info.txt`, `writeaudit`, and a guarded manual
  `writeunlock` command that is still off by default for real media.
- Includes an interactive shell plus standalone `/bin/hello.elf`,
  `/bin/ls.elf`, and `/bin/cat.elf` user programs.

## Documentation

See [docs/MANUAL.md](docs/MANUAL.md) for:

- User guide and shell command reference.
- Developer guide for writing user programs.
- User-space support library and syscall/API notes.
- Current limitations and real-hardware boot notes.

When a change affects user behavior or the user-space developer API, update the
manual in the same change. Internal-only refactors do not need manual updates
unless they alter observable behavior.

## Build And Run

Install the macOS toolchain:

```sh
brew install llvm lld qemu nasm xorriso mtools limine
```

Build the bootable ISO:

```sh
make
```

Run in QEMU:

```sh
make run
```

Useful targets:

```sh
make kernel
make init
make shell
make hello
make ls
make cat
make iso
make clean
make disk
make usb-gpt-disk
make test-exception
make test-page-fault
make test-user-fault
make test-user-programs
make test-exfat-commit
```

Build outputs:

```text
build/kernel.elf
build/init.elf
build/shell.elf
build/hello.elf
build/ls.elf
build/cat.elf
build/initrd.tar
build/TangPingOS.iso
build/disk.img
build/usb.img
```

Current startup chain:

```text
kernel -> init.elf -> /bin/shell.elf -> TangPingOS:/>
```
