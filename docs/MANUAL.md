# TangPingOS Manual

This document is both the user manual and the user-space developer guide for
TangPingOS.

## Quick Start

Build and boot in QEMU:

```sh
make clean && make iso
make run
```

After boot, TangPingOS runs a startup self-test and enters the shell:

```text
TangPingOS:/>
```

The prompt format is `TangPingOS:<cwd>>`. For example, `TangPingOS:/dev>` means
the current working directory is `/dev`.

On real hardware the framebuffer display is intentionally quiet during boot:
early status is shown briefly, detailed kernel/userland startup logs continue on
the serial console, and the framebuffer is cleared when the interactive shell is
ready. This avoids very slow full-screen scrolling on high-resolution UEFI GOP
framebuffers.

## Boot Flow

Current startup is:

```text
Limine -> kernel.elf -> init.elf -> /bin/shell.elf -> interactive shell
```

`kernel.elf`, `init.elf`, and `initrd.tar` are loaded by Limine. The kernel
parses `initrd.tar` as a ustar archive and exposes it through VFS. `init.elf`
opens `/dev/tty`, binds standard fds, and spawns `/bin/shell.elf`.

## Files And Devices

Current paths:

```text
/                 root directory
/dev              device directory
/dev/tty          keyboard/console TTY device
/motd.txt         readonly initrd file
/hello.txt        readonly initrd file
/bin              readonly program directory from initrd
/bin/shell.elf    interactive shell
/bin/hello.elf    demo user program
/bin/ls.elf       standalone directory listing program
/bin/cat.elf      standalone file printing program
```

Files created by shell commands under `/` live in ramfs. They disappear after
reboot. When QEMU provides the known USB test image, `/usbdisk` is a guarded
writable exFAT mount and files written there persist in `build/usb.img`.

TangPingOS also has a first block-device layer. `ramblk0` is an in-memory
disk-like device with 64 sectors of 512 bytes each. In QEMU, `make run` also
creates and attaches `build/disk.img` through virtio-blk; the kernel registers
that real virtual disk as `vd0` and parses MBR or basic GPT partitions as
`vd0p1`, `vd0p2`, and so on. The first QEMU test partition is mounted through a small exFAT
block-backed VFS skeleton at `/usb`. Normal builds still treat it as readonly,
while guarded test builds validate limited root-file create, write, grow, read,
and unlink paths. The QEMU test partition contains an exFAT-shaped boot region
with matching main/backup boot sectors and checksum sectors, plus a tiny root
directory sample so the kernel can validate exFAT
detection, root-directory chain parsing, subdirectory parsing, FAT-chain file
reads, allocation-bitmap updates, and small FAT-chain growth. The second
partition is mounted at `/boot` and is a small FAT32 sample that stands in for
the kind of UEFI boot partition a real machine commonly uses.

`mounts` shows the current VFS mount table. Today `/dev` is a real devfs mount,
`/usb` and `/boot` are real block-device-backed mounts when QEMU provides
`vd0p1` and `vd0p2`; `/usbdisk` and `/usbboot` are real USB Mass Storage-backed
mounts when QEMU provides the xHCI USB test disk. Writable `ramfs` plus
readonly `initrd` are layered at `/`. The current `/usb`
backend is not a full exFAT filesystem yet; it exposes test files, parsed
exFAT boot-sector metadata, and readonly root-directory files whose data can
span a FAT chain. The root directory itself can also span a FAT chain, and
simple readonly subdirectories are supported. The mount also parses the exFAT
allocation bitmap and reports scanned, used, free, and first-free cluster
counts in `/usb/info.txt`. It can also build a dry-run allocation plan for a
small future write, showing which free clusters would be reserved and how their
FAT chain would be linked. This plan is informational only; TangPingOS still
does not modify the exFAT partition. The mount also dry-runs the directory
entry side of creating `/NEW.TXT`, reporting the root-directory cluster, sector,
slot, and LBA where the three required exFAT entries would fit. It additionally
encodes those planned `0x85`, `0xc0`, and `0xc1` directory entries as hex so the
future write path can be checked before any sector is modified. Finally, it
summarizes the whole dry-run write transaction: the allocation-bitmap byte, FAT
entries, and directory-entry byte range that would be modified. The same
transaction is also applied to in-memory sector copies and checked back as a
dry-run patch, so the kernel can verify the resulting bitmap byte, FAT chain,
and `/NEW.TXT` directory metadata before normal real writes exist. A guarded
test build can additionally commit those three sector updates to the QEMU
`disk.img` and the QEMU USB image, then read them back for verification. In
that test build, the preallocated `/NEW.TXT` slot can be renamed once, and a
small test-only creator can allocate uppercase 8.3-style root files such as
`LOG.TXT` or `TWO.TXT` according to the requested size. The same guarded test
path can grow a one-cluster file such as `GROW.TXT` into a two-cluster FAT
chain, write across that boundary, read the contents back, and unlink those
writable test files by marking their directory entries inactive and freeing
their bitmap/FAT chain. Normal builds keep `/usb` readonly; `/usbdisk` is
writable only when the USB partition matches TangPingOS's known QEMU exFAT test
image whitelist. The current `/boot` backend
recognizes FAT32 boot-sector geometry, lists 8.3 short-name files and ASCII
long-file-name entries, walks readonly subdirectories, and reads file cluster
data. FAT32 lookups on `/boot` and `/usbboot` are ASCII case-insensitive, so
`cat /usbboot/readme.txt` can match a disk entry named `README.TXT`. It does
not yet support Unicode long names or FAT32 writes.

`make run` also attaches `build/usb.img` through QEMU xHCI `usb-storage`.
TangPingOS probes it through USB Mass Storage BOT, verifies a scratch-sector
SCSI `WRITE(10)` plus `READ(10)` round trip near the end of the test image,
registers it as `usb0`, scans MBR or GPT partitions as `usb0p1`, `usb0p2`, and
so on, mounts the first detected exFAT partition at `/usbdisk`, and mounts the
first detected FAT32 partition at `/usbboot` when probing succeeds. This path uses the USB/xHCI
read/write-sector implementation rather than virtio-blk. The `/usbdisk`
filesystem mount is writable in normal builds only for that known QEMU test
image; otherwise it stays readonly and reports `readonly_non_test_media` in
`/usbdisk/info.txt`. The same metadata file now also reports a write-readiness
audit: `write_candidate_ready` means the exFAT geometry, bitmap, root directory,
and dry-run transaction plan are coherent enough for the current limited writer;
`write_policy_allows_commit` is the actual safety gate; and `write_blocker`
names the first reason writes remain disabled. It also reports boot-region risk
signals such as `volume_dirty`, `media_failure`, `boot_checksum_valid`,
`backup_boot_sector_present`, `backup_boot_checksum_valid`,
`real_write_prereq_ready`, and `real_write_blocker`. These fields are
preconditions for a future real-media unlock path; they do not grant write
permission by themselves. On the generated QEMU test images these boot-region
checks should be valid so the remaining permission decision is isolated in the
write policy gate. The metadata also includes `real_write_dry_run_*` fields
that describe the currently planned root-file creation transaction, including
the action, path, metadata LBAs, cluster list, write count, and whether policy
would currently allow the write. This is still a report only. `/usbboot` is
readonly FAT32.

On non-QEMU xHCI controllers, TangPingOS uses a conservative real-hardware
probe path. It records whether BIOS/OS handoff was required and completed, then
only attempts controller halt/reset/start when that handoff state is safe. USB
Mass Storage devices discovered through this path are registered readonly; the
destructive scratch-sector `WRITE(10)` self-test is skipped, so real or unknown
USB media is not automatically modified.

## Shell Commands

| Command | Example | Function |
| --- | --- | --- |
| `help` | `help` | Prints the command list. |
| `pwd` | `pwd` | Prints the current working directory. |
| `cd` | `cd /dev` | Changes directory. `cd` with no argument returns to `/`; `.` and `..` are supported. |
| `ls` | `ls /bin` | Builtin directory listing. With no argument, lists the current directory. |
| `cat` | `cat /hello.txt` | Builtin file printer. |
| `echo` | `echo hello` | Prints text after the command. |
| `clear` | `clear` | Scrolls blank lines to visually clear the console. |
| `touch` | `touch note.txt` | Creates an empty ramfs file. |
| `write` | `write note.txt hello` | Creates or truncates a ramfs file and writes text. |
| `append` | `append note.txt world` | Creates a ramfs file if needed and appends text. |
| `rm` | `rm note.txt` | Removes a writable ramfs file. Readonly initrd files and device nodes are protected. |
| `stat` | `stat /bin/hello.elf` | Prints a path's type and size. |
| `cp` | `cp /hello.txt copy.txt` | Copies file contents to a new or truncated ramfs file. |
| `mv` | `mv copy.txt moved.txt` | Copies a writable source to a destination, then removes the source. |
| `hexdump` | `hexdump moved.txt` | Prints file bytes in hexadecimal. |
| `edit` | `edit note.txt` | Opens a simple line editor. Type lines, then `.save`. |
| `run` | `run /bin/hello.elf one two` | Starts an ELF program as a separate user task. |
| `ps` | `ps` | Lists tasks with PID, state, switch count, exit status, and name. |
| `mem` | `mem` | Prints physical page size, total pages, used pages, and free pages. |
| `uptime` | `uptime` | Prints scheduler ticks and whole seconds. |
| `sysinfo` | `sysinfo` | Prints CPU, framebuffer, memmap, timer, uptime, memory summary, and xHCI count. |
| `usb` | `usb` | Prints the first detected xHCI USB controller's PCI, MMIO, capability-register, operational-register/reset policy, real-hardware handoff/start result, extended-capability, BIOS/OS handoff, ring setup, first command result, root-hub port status including the first, second, and selected connected ports, QEMU port-reset information, first Address Device result, first device/configuration/interface descriptors, first bulk endpoint pair, Configure Endpoint result, Set Configuration result, and first USB Mass Storage BOT Inquiry/Read Capacity/Read10/Write10 results. |
| `usbdiag` | `usbdiag` | Prints a compact USB bring-up diagnosis with root-port power/rescan counters, selected root port, normal/warm reset status, address/configuration/BOT status, first-level USB hub status, whether `usb0` exists, and a short reason such as `no_connected_root_port`, `usb_hub_second_slot_address_failed`, `mass_storage_inquiry_failed_or_not_attempted`, or `usb0_ready`. |
| `mounts` | `mounts` | Lists VFS mount entries, sources, and writable status. |
| `lsblk` | `lsblk` | Lists registered block devices. |
| `blkread` | `blkread 2 0` | Reads one 512-byte sector and prints it in hexadecimal. After USB Mass Storage probing succeeds, `blkread <usb0-id> 0` can read the `usb0` device shown by `lsblk`. |
| `blkwrite` | `blkwrite 2 2 hello` | Writes text into one 512-byte sector of a writable block device. |
| `usbtestwrite` | `usbtestwrite` | Safely writes and reads back the final scratch sector of the QEMU `usb0p1` test partition. It refuses non-test USB partition sizes and is not a real-USB write command. |
| `writeaudit` | `writeaudit [/mount]` | Reads a mount's `info.txt` and summarizes exFAT write policy, blocker, dry-run LBA list, and cluster list. The default mount is `/usbdisk`; this command is audit-only and never unlocks real USB writes. |
| `writeunlock` | `writeunlock /usbdisk <bitmap_lba> <fat_lba> <directory_lba> TPUSBWRITE` | Requests the guarded exFAT manual write unlock for a USB blockfs mount. The LBA values must come from `writeaudit`; this is a dangerous development command and still only supports the current root uppercase 8.3 exFAT writer. |

Single-path commands such as `cd`, `ls`, `cat`, `stat`, `rm`, `touch`, `edit`,
and `hexdump` accept double quotes around a path, for example
`cat "/boot/very long filename.txt"`.

When QEMU provides the test disk, `/usb` contains:

| Path | Function |
| --- | --- |
| `/usb/info.txt` | Text metadata for the block-backed mount, source device, exFAT boot sector, allocation bitmap/free-space summary, and upcase table when detected. |
| `/usb/sector0.bin` | Raw bytes from LBA 0 of the mounted block device; in QEMU this starts with an exFAT boot sector. |
| `/usb/HELLO.TXT` | Parsed from the QEMU test partition's exFAT root directory and readable as a small first-cluster file. |
| `/usb/CHAIN.TXT` | Parsed from the QEMU test partition and readable through a two-cluster FAT chain. |
| `/usb/LATE.TXT` | Parsed from a later root-directory cluster, proving that the root directory chain is followed. |
| `/usb/DIR/INNER.TXT` | Parsed from a readonly exFAT subdirectory. |

When QEMU provides the USB storage test image, `/usbdisk` contains:

| Path | Function |
| --- | --- |
| `/usbdisk/info.txt` | Text metadata for the USB-backed exFAT mount. |
| `/usbdisk/HELLO.TXT` | Parsed from the USB BOT-backed exFAT partition. |
| `/usbdisk/CHAIN.TXT` | Read through a two-cluster FAT chain over USB BOT sector reads. |
| `/usbdisk/LATE.TXT` | Parsed from the second root-directory cluster over USB BOT reads. |

On the known QEMU USB test image, `/usbdisk` is writable in normal builds:

```text
cat /usbdisk/info.txt
writeaudit
write /usbdisk/NOTE.TXT hello
cat /usbdisk/NOTE.TXT
append /usbdisk/NOTE.TXT world
cp /hello.txt /usbdisk/COPY.TXT
mv /usbdisk/COPY.TXT /usbdisk/MOVED.TXT
stat /usbdisk/MOVED.TXT
rm /usbdisk/MOVED.TXT
edit /usbdisk/EDIT.TXT
```

The current `/usbdisk` writer is deliberately narrow: it only accepts root
directory files with uppercase ASCII 8.3 names such as `NOTE.TXT`; it does not
create subdirectories, long filenames, Unicode names, or FAT32 files. Writes
are limited to the small managed cluster chains used by the test image. Unknown
or real USB media stays readonly by default; check `/usbdisk/info.txt` for
`write_enabled`, `write_policy`, `commit_ready`, `write_candidate_ready`,
`write_policy_allows_commit`, `write_blocker`, `real_write_prereq_ready`, and
`real_write_blocker`. It also reports `real_write_dry_run_action`,
`real_write_dry_run_path`, `real_write_dry_run_lbas`,
`real_write_dry_run_clusters`, `real_write_dry_run_ready`,
`real_write_dry_run_policy_allows`, `real_write_dry_run_note`,
`manual_unlock_attempted`, `manual_unlock_ready`, and
`manual_unlock_blocker`. A real USB drive may eventually show
`write_candidate_ready: 1`, but it remains non-writable unless you explicitly
request the manual unlock and every kernel-side check passes. The `writeaudit`
command is the shorter way to inspect the same decision points: it prints the
write policy, blocker, manual unlock status, candidate readiness, dry-run
metadata sectors, planned clusters, and final result without dumping the entire
metadata file.

The guarded manual unlock flow is:

```text
writeaudit /usbdisk
writeunlock /usbdisk <bitmap_lba> <fat_lba> <directory_lba> TPUSBWRITE
mounts
```

Copy the three LBA values from `dry_run_lbas`. The kernel rejects the request if
the mount is not exFAT, the source is not a USB partition such as `usb0p1`, the
exFAT real-write prerequisites are not ready, the LBA values do not match the
current dry-run plan, or the block device cannot safely expose a write path.
This only unlocks TangPingOS's current narrow exFAT root-file writer; it does
not add FAT32 writes, subdirectory creation, long filename creation, Unicode
filename creation, arbitrary-size files, UASP, hubs, or hotplug support.

On real hardware, use these commands to inspect the readonly USB path:

```text
usb
usbdiag
lsblk
mounts
blkread <usb0-id> 0
writeaudit
cat /usbdisk/info.txt
cat /usbboot/info.txt
```

The `usb` command prints `reset_policy`, `real_hw_attempted`,
`handoff_required`, `controller_start_result`, `bot_last_error_stage`, and
`usb_storage_readonly_policy`. It also prints `selected connected port`,
`selected speed`, and `selected score`; TangPingOS uses those fields to choose
which root port to reset/address when more than one port reports a connected
device. If `lsblk` does not show a `usb0` block device, then TangPingOS has not
reached partition scanning or exFAT mounting yet, so `mounts` cannot show
`/usbdisk` even if the physical USB stick is formatted as exFAT. For real or
unknown USB devices, writable mount status should stay `0`; attempts such as
`write /usbdisk/NOTE.TXT hello` must fail unless the media matches TangPingOS's
known QEMU test image whitelist.
For exFAT media, `/usbdisk/info.txt` also prints `write_audit_*` fields for
volume, FAT, cluster heap, root directory, allocation bitmap, and upcase-table
bounds. It also checks whether the exFAT volume is dirty, whether media failure
is flagged, whether the main and backup boot checksum sectors validate, and
whether the backup boot sector matches the primary boot sector. These are
diagnostic readiness checks, not permission to write. The QEMU test images are
generated with valid main and backup exFAT boot checksums so these fields can
be tested before any real-media write unlock exists. The dry-run fields show
which bitmap, FAT, and directory sectors would be touched by the current
limited root-file create transaction, but TangPingOS still refuses writes on
unknown real media until `writeunlock` is invoked explicitly and succeeds.

TangPingOS can now recognize a first-level external USB hub, set its
configuration, read the minimum hub descriptor header, power its downstream
ports, find the first connected downstream port, request a downstream port
reset, and attempt second-slot xHCI enumeration for the device behind that hub.
It first requests the normal USB2 hub descriptor and then falls back to the
SuperSpeed hub descriptor type if needed. This is still not full hub support:
hub paths remain under active bring-up, and `usbdiag` should be used to see
whether the current failure is root-port reset, hub reset, second-slot address,
mass-storage configuration, or BOT/SCSI. Booting from a USB stick through UEFI
also does not mean the kernel can keep using that stick:
after boot, TangPingOS currently relies on its own xHCI/BOT path rather than
UEFI disk services.
When `usbdiag` reports `no_connected_root_port`, the controller has started but
no root port reported a connected device after TangPingOS powered root ports and
performed bounded rescans.

The same USB storage test image also contains a FAT32 partition mounted at
`/usbboot`:

| Path | Function |
| --- | --- |
| `/usbboot/info.txt` | Text metadata for the USB-backed FAT32 mount. |
| `/usbboot/README.TXT` | Readonly FAT32 root-directory short-name file over USB BOT reads. |
| `/usbboot/KERNEL.TXT` | Second readonly USB FAT32 root-directory file. |
| `/usbboot/usb long filename.txt` | Readonly USB FAT32 long-file-name sample. |
| `/usbboot/CHAIN.TXT` | Readonly USB FAT32 file whose data spans a two-cluster FAT chain. |
| `/usbboot/LATE.TXT` | Readonly USB FAT32 file parsed from the second root-directory cluster. |
| `/usbboot/EFI/BOOT/BOOTX64.EFI` | Readonly USB FAT32 subdirectory sample matching the UEFI removable-media boot path. |

FAT32 paths under `/usbboot` are ASCII case-insensitive. For example,
`cat /usbboot/readme.txt` and `cat /usbboot/efi/boot/bootx64.efi` work even
though the sample image stores uppercase short-name directory entries.

The same QEMU disk also contains a FAT32 sample mounted at `/boot`:

| Path | Function |
| --- | --- |
| `/boot/info.txt` | Text metadata for the FAT32 mount, including bytes per sector, FAT size, root cluster, hidden sectors, and volume label. |
| `/boot/README.TXT` | Readonly FAT32 root-directory short-name file. |
| `/boot/KERNEL.TXT` | Second readonly FAT32 root-directory short-name file. |
| `/boot/very long filename.txt` | Readonly FAT32 long-file-name sample spanning multiple LFN entries. |
| `/boot/CHAIN.TXT` | Readonly FAT32 file whose data spans a two-cluster FAT chain. |
| `/boot/LATE.TXT` | Readonly FAT32 file parsed from the second root-directory cluster. |
| `/boot/EFI/BOOT/BOOTX64.EFI` | Readonly FAT32 subdirectory sample matching the usual UEFI removable-media boot path. |

FAT32 paths under `/boot` use the same ASCII case-insensitive lookup. For
example, `cat /boot/readme.txt` and `cat /boot/efi/boot/bootx64.efi` work.

Standalone programs can be launched with `run`:

```text
run /bin/hello.elf one two
run /bin/ls.elf /bin
run /bin/cat.elf /hello.txt
```

Current `run` argument parsing is simple: arguments are split on spaces; quotes,
escaping, and environment variables are not implemented.

## Example Session

```text
help
ls /
ls /bin
cat /motd.txt
write note.txt TangPingOS
append note.txt works
cat note.txt
cp note.txt copy.txt
mv copy.txt moved.txt
hexdump moved.txt
edit draft.txt
stat note.txt
ps
mem
uptime
sysinfo
usb
lsblk
blkread 2 0
blkwrite 2 2 TangPingOS
blkread 2 2
run /bin/ls.elf /bin
run /bin/cat.elf /hello.txt
rm note.txt
```

## Writing User Programs

User programs are freestanding x86_64 ELF binaries linked with
`linker/user.ld`. They run in Ring 3 and talk to the kernel through `int 0x80`
syscalls. Use the shared support library in `userspace/lib`.

Minimal program:

```c
#include "tpos.h"

__attribute__((noreturn))
void _start(uint64_t argc, char **argv) {
    tpos_write_cstr("hello from user space\n");
    tpos_exit(0);
}
```

Arguments:

- `argc` is the number of arguments.
- `argv[0]` is the program path.
- Extra words passed through `run /path.elf arg1 arg2` become `argv[1]`,
  `argv[2]`, and so on.

Add a new program today by following the existing pattern:

1. Create `userspace/name/name.c`.
2. Include `tpos.h`.
3. Implement `_start(uint64_t argc, char **argv)`.
4. Add a build variable, object list, target, and initrd copy step in
   `Makefile`.
5. Rebuild with `make clean && make iso`.
6. Run it from the shell with `run /bin/name.elf`.

This build wiring is still manual. A later cleanup should make `/bin/*.elf`
registration more automatic.

## User-Space Library

Header:

```c
#include "tpos.h"
```

Common constants:

```text
STDIN_FD  = 0
STDOUT_FD = 1
STDERR_FD = 2

OPEN_CREATE = 1
OPEN_TRUNC  = 2
OPEN_APPEND = 4

SEEK_SET = 0
SEEK_CUR = 1
SEEK_END = 2
```

Important types:

```c
struct dirent;
struct task_info;
struct meminfo;
struct system_info;
struct mount_info;
struct block_device_info;
```

Common helpers:

```c
uint64_t tpos_write(uint64_t fd, const char *buffer, uint64_t length);
void tpos_write_cstr(const char *s);
void tpos_write_literal(const char *message, uint64_t length);
void tpos_write_u64_decimal(uint64_t value);
void tpos_write_u64_hex(uint64_t value);
uint64_t tpos_strlen(const char *s);
uint64_t tpos_strcpy(char *dst, const char *src);
__attribute__((noreturn)) void tpos_exit(uint64_t status);
```

Filesystem and process helpers:

```c
uint64_t tpos_open(const char *path);
uint64_t tpos_open_flags(const char *path, uint64_t flags);
uint64_t tpos_read(uint64_t fd, void *buffer, uint64_t length);
uint64_t tpos_close(uint64_t fd);
uint64_t tpos_getdents(const char *path, uint64_t index, struct dirent *dirent);
uint64_t tpos_write_file(const char *path, uint64_t offset, const void *buffer, uint64_t length);
uint64_t tpos_dup2(uint64_t old_fd, uint64_t new_fd);
uint64_t tpos_lseek(uint64_t fd, int64_t offset, uint64_t whence);
uint64_t tpos_unlink(const char *path);
uint64_t tpos_spawn(const char *path, const char *args);
```

System helpers:

```c
uint64_t tpos_getpid(void);
void tpos_yield(void);
void tpos_sleep_ticks(uint64_t ticks);
uint64_t tpos_brk(uint64_t new_break);
void *tpos_sbrk(uint64_t increment);
uint64_t tpos_task_info(uint64_t index, struct task_info *info);
uint64_t tpos_meminfo(struct meminfo *info);
uint64_t tpos_system_info(struct system_info *info);
uint64_t tpos_uptime(void);
uint64_t tpos_mount_info(uint64_t index, struct mount_info *info);
```

`tpos_mount_info` returns mount entries by numeric index. It reports each
mounted filesystem's path, filesystem name, source, and writable flag. It
returns `-1` when the index is past the end of the table.

Block-device helpers:

```c
uint64_t tpos_block_info(uint64_t index, struct block_device_info *info);
uint64_t tpos_block_read(uint64_t index, uint64_t lba, void *buffer, uint64_t count);
uint64_t tpos_block_write(uint64_t index, uint64_t lba, const void *buffer, uint64_t count);
uint64_t tpos_blockfs_unlock(const char *mount_path,
                             const struct blockfs_unlock_request *request);
```

Current block devices are addressed by numeric index. `ramblk0` is index `0`;
when QEMU virtio-blk is present, `vd0` is index `1`, and the first MBR/GPT
partition `vd0p1` is index `2`. The current shell commands use one-sector
operations; the syscall ABI already keeps a sector count argument for later
storage work.

`tpos_blockfs_unlock` is a development-only guard around blockfs write enabling.
Fill `struct blockfs_unlock_request` with `BLOCKFS_UNLOCK_MAGIC`, the three
dry-run LBA values reported by `writeaudit`, and `flags = 0`. The kernel still
rejects the request unless the mount is a compatible USB exFAT partition and
the readonly audit says the current limited transaction is ready.

## Syscall ABI

TangPingOS currently uses `int 0x80`.

```text
rax = syscall number
rdi = arg0
rsi = arg1
rdx = arg2
rcx = arg3
rax = return value
```

Current syscall numbers:

```text
1  write(fd, buf, len)
2  exit(status)
3  getpid()
4  yield()
5  sleep_ticks(ticks)
6  brk(new_break)
7  read_file(path, offset, buf, len)
8  open(path, flags)
9  read(fd, buf, len)
10 close(fd)
11 getdents(path, index, dirent, len)
12 write_fd(fd, buf, len)
13 dup2(oldfd, newfd)
14 write_file(path, offset, buf, len)
15 lseek(fd, offset, whence)
16 unlink(path)
17 spawn(path, args)
18 task_info(index, buf, len)
19 meminfo(buf, len)
20 sysinfo(buf, len)
21 uptime()
22 block_info(index, buf, len)
23 block_read(index, lba, buf, count)
24 block_write(index, lba, buf, count)
25 mount_info(index, buf, len)
26 blockfs_unlock(path, request, len)
```

Use `tpos.h` wrappers instead of issuing raw syscalls unless you are
deliberately testing the ABI.

## Test Targets

```sh
make test-exception
make test-page-fault
make test-user-fault
make test-user-programs
make test-exfat-commit
make usb-gpt-disk
```

- `test-exception` builds a kernel that triggers an invalid-opcode exception.
- `test-page-fault` builds a kernel that triggers a page fault.
- `test-user-fault` builds user programs so `shell.elf` triggers a user-mode
  null write; the kernel should kill only that process.
- `test-user-programs` makes the shell startup self-test spawn `/bin/ls.elf`
  and `/bin/cat.elf`.
- `test-exfat-commit` enables the guarded exFAT transaction commit path for the
  QEMU test disk and QEMU USB test image; it writes the planned `/NEW.TXT`
  bitmap, FAT, and directory sectors, reads them back to verify the commit, and
  then lets the shell create `/usb/NOTE.TXT`, `/usb/LOG.TXT`,
  `/usb/GROW.TXT`, `/usbdisk/USB.TXT`, `/usbdisk/TWO.TXT`, and
  `/usbdisk/GROW.TXT` through the normal VFS file API, write text, grow a small
  file across a cluster boundary, read the bytes back, unlink the dynamic files,
  recreate them, and run a bounded USB scratch-sector stress loop that forces
  xHCI transfer/event ring wraparound.

After any test target, rebuild normal output with:

```sh
make clean && make iso
```

## QEMU Virtual Disk

`make run` builds `build/disk.img` if needed and attaches it as a legacy
virtio-blk PCI device. The image contains a simple MBR with two primary
partitions. `vd0p1` begins at LBA 2048 and contains the exFAT-shaped test
filesystem used by `/usb`, including valid primary and backup exFAT boot
regions/checksum sectors for write-readiness auditing. `vd0p2` begins at LBA
11264 and contains the FAT32 sample used by `/boot`. The exFAT partition has
root-directory entries for `HELLO.TXT`, `CHAIN.TXT`, `LATE.TXT`, and `DIR` for
parser testing.
`CHAIN.TXT` deliberately uses a two-cluster FAT chain, `LATE.TXT` deliberately
lives in a later root-directory cluster, and `DIR/INNER.TXT` tests subdirectory
parsing. The FAT32 partition has short-name root files `README.TXT`,
`KERNEL.TXT`, `CHAIN.TXT`, and `LATE.TXT`, an ASCII long-name file
`very long filename.txt`, plus an `EFI/BOOT/BOOTX64.EFI` subdirectory sample.
`CHAIN.TXT` deliberately spans a two-cluster FAT chain, and `LATE.TXT`
deliberately lives in the second root-directory cluster. The kernel scans PCI
config space, initializes the virtqueue, registers the disk as `vd0`, parses
the MBR or GPT partition table, and registers `vd0p1` and `vd0p2`. `make run` also builds
`build/usb.img`, attaches it as QEMU xHCI `usb-storage`, runs a scratch-sector
USB BOT `WRITE(10)` readback check, registers the USB disk as `usb0`, parses
its MBR or GPT partitions as `usb0pN`, mounts the first detected exFAT
partition at `/usbdisk`, and mounts the first detected FAT32 partition at
`/usbboot`.

`make usb-gpt-disk` builds `build/usb-gpt.img` with the same exFAT and FAT32
partition contents but a protective MBR plus GPT partition table. It is useful
for checking the GPT scanner with:

```sh
make run USB_DISK=build/usb-gpt.img
```

Useful checks in the shell:

```text
mounts
ls /usb
cat /usb/info.txt
cat /usb/HELLO.TXT
stat /usb/CHAIN.TXT
cat /usb/LATE.TXT
ls /usb/DIR
cat /usb/DIR/INNER.TXT
ls /boot
cat /boot/info.txt
cat /boot/README.TXT
cat /boot/readme.txt
cat "/boot/very long filename.txt"
cat /boot/CHAIN.TXT
cat /boot/LATE.TXT
ls /boot/EFI
ls /boot/EFI/BOOT
cat /boot/EFI/BOOT/BOOTX64.EFI
cat /boot/efi/boot/bootx64.efi
ls /usbdisk
cat /usbdisk/info.txt
writeaudit /usbdisk
cat /usbdisk/HELLO.TXT
write /usbdisk/NOTE.TXT hello
append /usbdisk/NOTE.TXT world
cat /usbdisk/NOTE.TXT
cp /hello.txt /usbdisk/COPY.TXT
mv /usbdisk/COPY.TXT /usbdisk/MOVED.TXT
stat /usbdisk/MOVED.TXT
rm /usbdisk/MOVED.TXT
ls /usbboot
cat /usbboot/info.txt
cat /usbboot/README.TXT
cat /usbboot/readme.txt
cat "/usbboot/usb long filename.txt"
cat /usbboot/CHAIN.TXT
cat /usbboot/LATE.TXT
ls /usbboot/EFI/BOOT
cat /usbboot/EFI/BOOT/BOOTX64.EFI
cat /usbboot/efi/boot/bootx64.efi
hexdump /usb/sector0.bin
lsblk
blkread 2 0
blkwrite 2 2 hello-from-vd0p1
blkread 2 2
usbtestwrite
```

This is real sector IO to a QEMU disk-image partition. `/usb` is now a mounted
block-device VFS that can identify an exFAT boot sector, list root-directory
entries across the root-directory FAT chain, and read files by following their
FAT cluster chain. It also supports simple readonly subdirectories and parses
the exFAT allocation bitmap well enough to count used/free clusters, find the
first free cluster, and produce a dry-run allocation plan such as
`12->13->14->EOF`. It can also dry-run the directory-entry placement for a new
root file such as `/NEW.TXT` and show the encoded directory-entry bytes. It
also summarizes the transaction as bitmap, FAT, and directory sector updates,
then applies those updates to in-memory sector copies to verify the resulting
bitmap, FAT chain, and directory metadata. The developer-only
`make test-exfat-commit` target enables a guarded QEMU test commit that writes
those three sectors to `build/disk.img` and `build/usb.img`, then verifies them
by reading the sectors back. In that same test build, the shell also opens
`/usb/NOTE.TXT`, `/usb/LOG.TXT`, `/usb/GROW.TXT`, `/usbdisk/USB.TXT`,
`/usbdisk/TWO.TXT`, and `/usbdisk/GROW.TXT` through VFS, writes text to them,
grows the `GROW.TXT` files from one cluster to two clusters, reads the exact
bytes back, unlinks the dynamic files, and recreates them. It also performs a
bounded USB scratch-sector stress loop to force xHCI event, bulk-in, and
bulk-out ring wraparound. Normal builds keep `/usb` readonly, but now mount the
known QEMU USB exFAT test partition `/usbdisk` as writable.

## Real Hardware Boot Notes

TangPingOS can theoretically boot on a real x86_64 UEFI PC from a USB stick,
but QEMU is still the primary supported environment.

High-level steps:

1. Build the ISO with `make clean && make iso`.
2. Write `build/TangPingOS.iso` to a USB stick as a raw disk image.
3. Boot the target PC's UEFI boot menu and choose the USB device.
4. Disable Secure Boot if the firmware refuses to boot unsigned EFI binaries.

On macOS, the dangerous raw-write flow looks like this:

```sh
diskutil list
diskutil unmountDisk /dev/diskN
sudo dd if=build/TangPingOS.iso of=/dev/rdiskN bs=4m status=progress
sync
diskutil eject /dev/diskN
```

Replace `diskN` with the USB disk, not an internal disk. Choosing the wrong disk
will destroy data.

Expected constraints on real hardware:

- The machine must be x86_64. Apple Silicon Macs cannot boot this OS directly.
- UEFI GOP framebuffer is expected; this is how the screen output is obtained.
- The framebuffer console is simple and CPU-rendered. TangPingOS reduces real
  hardware display cost in three layers: verbose boot logs are kept off the
  framebuffer until the interactive shell is ready, the framebuffer is remapped
  with x86 PAT write-combining for faster sequential pixel writes, and console
  text is stored in a shadow character buffer so scrolling updates RAM first and
  only redraws dirty rows to the framebuffer. Normal kernel info logs are kept
  on the serial console so they do not overwrite the interactive screen.
- Secure Boot is not supported.
- Real hardware disk-controller access is not implemented. The current
  non-memory disk path is QEMU virtio-blk only; the OS still uses the initrd
  packed into the boot image for startup content.
- The kernel can now detect xHCI USB controllers through PCI, map the first
  controller's MMIO registers, report capability, operational, and extended
  capability registers with `usb`, and perform BIOS/OS xHCI handoff when a USB
  Legacy Support capability is present. QEMU xHCI keeps the full writable test
  path; non-QEMU xHCI controllers use a conservative readonly probe path and do
  not run destructive USB write self-tests. TangPingOS can now allocate and
  register the first command ring, event ring, event ring segment table, and
  device context base address array for QEMU xHCI, then start the controller
  and issue the first `Enable Slot` command. It can also scan xHCI root-hub
  `PORTSC` registers to report connected, enabled, powered, and first-port
  speed/link-state information for the first two connected ports. QEMU runs now
  attach small `usb-storage` and `usb-kbd` test devices to the xHCI controller,
  and TangPingOS can see both connected ports before resetting the first
  connected QEMU xHCI port. TangPingOS can also build the initial xHCI
  input context for that port, issue `Address Device`, and report the assigned
  USB address and slot state. It can now issue a first EP0
  `GET_DESCRIPTOR(Device)` request and report the device descriptor's
  vendor/product/class fields. It can also issue a first
  `GET_DESCRIPTOR(Configuration)` request and report the configuration total
  length, interface count, first interface class/subclass/protocol, and the
  first bulk-in/bulk-out endpoint addresses. It can now allocate bulk transfer
  rings, build xHCI endpoint contexts for the first bulk-in/bulk-out pair, and
  issue the first `Configure Endpoint` command. It can now send USB
  `SET_CONFIGURATION(1)` and issue a first USB Mass Storage BOT/SCSI `INQUIRY`
  command over the configured bulk endpoints. It can also issue SCSI
  `READ CAPACITY(10)`, `READ(10)`, and `WRITE(10)` over BOT to prove the QEMU
  USB-storage device's block count/block size and sector I/O path. The
  successful USB-storage path is exposed as block device `usb0`, so `lsblk`
  can list it and `blkread`/`blkwrite` can access sectors through the generic
  block syscall path. TangPingOS also scans `usb0` for MBR or GPT partitions,
  mounts the first detected USB exFAT partition at `/usbdisk`, and mounts the
  first detected USB FAT32 partition at `/usbboot`. The `usbtestwrite` command performs a bounded
  write/readback check against the last sector of the known QEMU `usb0p1` test
  partition and refuses other partition sizes. Normal builds can create,
  overwrite, append, grow, read, and unlink limited uppercase 8.3 root files on
  `/usbdisk` only when the partition matches the known QEMU test-image
  whitelist; unknown USB media is mounted readonly. `/usbboot` is readonly
  FAT32. It does not yet use USB HID keyboard input.
- Keyboard input currently depends on simple PS/2-style input. Many modern
  laptops use USB/xHCI or firmware translation, so keyboard behavior on real
  hardware is uncertain until TangPingOS has USB HID support.
- Serial logs visible in QEMU will usually not be visible on a normal laptop
  unless it has a usable serial console.

For a live demo today, QEMU is reliable. For a USB demo on a real PC, test on the
exact hardware in advance and bring a fallback QEMU demo.

## Current Limitations

- Persistent writes are limited to the whitelisted QEMU `/usbdisk` exFAT test
  image. Real unknown USB media is still mounted readonly even when its
  `/usbdisk/info.txt` write audit says the metadata is a plausible write
  candidate.
- MBR primary partitions and basic GPT partition arrays are parsed, but MBR
  extended partitions, GPT CRC validation, partition labels, and partition GUID
  policy decisions are not implemented yet.
- `/usb` is a block-backed VFS with minimal exFAT boot-sector,
  allocation-bitmap, dry-run allocation planning, directory-entry byte
  encoding, dry-run transaction summaries, in-memory transaction patch
  validation, upcase-table, root-directory chain, subdirectory, and FAT-chain
  parsing, not a complete exFAT filesystem. The `make test-exfat-commit`
  build can commit one fixed allocation transaction to the QEMU virtio test
  disk and USB test image, then create a few guarded uppercase 8.3-style root
  files, grow a small file from one cluster to two clusters, and unlink those
  test-created files. Normal builds keep `/usb` readonly, but open the same
  limited root-file writer on `/usbdisk` when the QEMU USB test image is
  whitelisted. This is not general-purpose exFAT write support.
- `/boot` and `/usbboot` are readonly FAT32 sample mounts with boot-sector
  parsing, 8.3 short-name directory listing, ASCII long-file-name entries,
  subdirectory walking, multi-cluster root directory traversal, FAT-chain file
  reads, and regular file reads. They do not yet support Unicode long names or
  writes.
- No real hardware disk-controller driver; QEMU virtio-blk is supported for
  disk-image sector IO.
- USB support is still narrow: xHCI plus USB Mass Storage BOT only, no UASP,
  hubs, hotplug, or real-media writes. Non-QEMU media is readonly by
  default.
- No full keyboard layout, modifiers, command history, or tab completion.
- No `wait`, pipes, redirection, signals, or environment variables.
- `edit` is a line editor, not a full-screen editor.
- `/bin` program registration in `Makefile` is still manual.
- Shell builtins and standalone programs currently coexist; builtins have not
  yet been fully moved into `/bin`.
