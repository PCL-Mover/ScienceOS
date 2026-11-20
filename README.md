# ScienceOS

ScienceOS 是基于 [TangPingOS](https://github.com/1wpc/TangPingOS) 的 x86_64 操作系统项目。

- 上游项目：<https://github.com/1wpc/TangPingOS>
- 本仓库：<https://github.com/PCL-Mover/ScienceOS>

在 TangPingOS 的基础上，本项目完成了：

- 引导从 Limine 迁移到 GRUB（Multiboot / Multiboot2），修复 GRUB 内存映射未保留内核/模块导致的覆盖问题
- 内核引导栈改为高半核栈，修复切换用户页表时的缺页
- tty 行缓冲与行编辑（退格不再吃掉提示符），并支持串口输入（COM1 / IRQ4）
- 8×16 IBM VGA 点阵字体
- bash 风格提示符 `root@x86:/#`、`用户@x86:路径$`
- 多用户账户系统

## 当前状态

- x86_64 UEFI，GRUB 引导，framebuffer + 串口输出
- GDT/IDT、异常报告、PIC/PIT、PS/2 键盘、串口输入、抢占式轮转调度
- Ring 3 ELF 用户程序，每进程独立地址空间
- VFS：devfs、initrd、可写 ramfs；FAT32 与 exFAT；virtio-blk、xHCI USB 存储
- 账户系统：
  - `/etc/passwd`、`/etc/shadow`（SHA-256 加盐）、`/etc/group`
  - 开机 `login`，密码输入不回显
  - shell 命令：`whoami`、`id`、`groups`、`su`、`passwd`、`useradd`
  - 默认账号 `root`，密码 `root`；`useradd` 新建用户初始密码 `changeme`
- 交互式 shell，以及 `/bin/hello.elf`、`/bin/ls.elf`、`/bin/cat.elf`

已知限制：

- 账户数据存放在 ramfs（内存），重启后恢复默认，尚未持久化到磁盘
- 尚未实现文件权限位（`chmod` / `ls -l` 与 open/exec 校验）
- ramfs 为扁平结构，`useradd` 不创建家目录

## 构建与运行

依赖（macOS 示例）：

```sh
brew install llvm lld qemu nasm xorriso mtools
```

还需要 x86_64-elf 的 GRUB 工具（`x86_64-elf-grub-mkstandalone` 与模块目录），
默认路径为 `GRUB_PREFIX ?= /Users/mac/opt/grub-x86_64-elf`，可用命令行覆盖：

```sh
make GRUB_PREFIX=/path/to/grub-x86_64-elf
```

构建 ISO：

```sh
make iso
```

产物：`build/TangPingOS.iso`

启动（UEFI + OVMF；macOS 上 QEMU 图形窗口直接可用）：

```sh
qemu-system-x86_64 -M q35 -m 512M -cdrom build/TangPingOS.iso \
  -display cocoa -no-reboot -no-shutdown \
  -drive if=pflash,format=raw,readonly=on,file=$(brew --prefix qemu)/share/qemu/edk2-x86_64-code.fd \
  -drive file=build/disk.img,if=none,id=vd0,format=raw \
  -drive file=build/usb.img,if=none,id=usb0,format=raw \
  -device qemu-xhci,id=xhci \
  -device usb-storage,bus=xhci.0,drive=usb0 \
  -device virtio-blk-pci,drive=vd0,disable-modern=on,disable-legacy=off
```

注意：不要同时挂 `-device usb-kbd`，QEMU 会把图形窗口按键只投递给 USB 键盘，
而内核目前只有 PS/2 键盘驱动。

## 启动流程

```text
kernel -> init.elf -> /bin/login.elf -> (认证通过) /bin/shell.elf
```

登录示例：

```text
x86 login: root
Password:
root@x86:/# whoami
root
root@x86:/# useradd alice
useradd: created alice uid=1000 (initial password: changeme)
root@x86:/# su alice
Password:
alice@x86:/$
```

## 文档

- [docs/MANUAL.md](docs/MANUAL.md)：用户指南、shell 命令、用户程序开发与本仓库限制

## 许可与致谢

代码源自 [TangPingOS](https://github.com/1wpc/TangPingOS)，遵循其原有许可。
8×16 点阵字模取自公开的 IBM VGA 字体数据。
