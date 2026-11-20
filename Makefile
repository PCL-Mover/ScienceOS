ARCH := x86_64
BUILD_DIR := build
ISO_ROOT := $(BUILD_DIR)/iso_root
INITRD_ROOT := $(BUILD_DIR)/initrd_root
KERNEL := $(BUILD_DIR)/kernel.elf
INIT := $(BUILD_DIR)/init.elf
USER_SHELL := $(BUILD_DIR)/shell.elf
HELLO := $(BUILD_DIR)/hello.elf
LS := $(BUILD_DIR)/ls.elf
CAT := $(BUILD_DIR)/cat.elf
LOGIN := $(BUILD_DIR)/login.elf
INITRD := $(BUILD_DIR)/initrd.tar
ISO := $(BUILD_DIR)/TangPingOS.iso
GRUB_EFI := $(BUILD_DIR)/bootx64.efi
EFI_IMG := $(BUILD_DIR)/efi.img
DISK := $(BUILD_DIR)/disk.img
USB_DISK := $(BUILD_DIR)/usb.img
USB_GPT_DISK := $(BUILD_DIR)/usb-gpt.img

BREW_PREFIX := $(shell brew --prefix 2>/dev/null)
LLVM_PREFIX := $(shell brew --prefix llvm 2>/dev/null)
GRUB_PREFIX ?= /Users/mac/opt/grub-x86_64-elf
QEMU_PREFIX := $(shell brew --prefix qemu 2>/dev/null)

CC := $(firstword $(wildcard $(LLVM_PREFIX)/bin/clang) $(shell command -v clang 2>/dev/null) clang)
LD := $(firstword $(shell command -v ld.lld 2>/dev/null) $(wildcard $(LLVM_PREFIX)/bin/ld.lld) ld.lld)
QEMU := qemu-system-x86_64
XORRISO := xorriso
GRUB_MKSTANDALONE := $(GRUB_PREFIX)/bin/x86_64-elf-grub-mkstandalone
GRUB_MODDIR := $(GRUB_PREFIX)/lib/x86_64-elf/grub/x86_64-efi
MFORMAT := mformat
MCOPY := mcopy
MMD := mmd
TAR := tar

OVMF_CODE := $(firstword \
	$(wildcard $(QEMU_PREFIX)/share/qemu/edk2-x86_64-code.fd) \
	$(wildcard $(QEMU_PREFIX)/share/qemu/edk2-x86_64-code.fd.bz2) \
	$(wildcard $(BREW_PREFIX)/share/qemu/edk2-x86_64-code.fd) \
	$(wildcard $(BREW_PREFIX)/share/qemu/edk2-x86_64-code.fd.bz2) \
	$(wildcard /opt/homebrew/share/qemu/edk2-x86_64-code.fd) \
	$(wildcard /usr/local/share/qemu/edk2-x86_64-code.fd) \
	$(wildcard /opt/local/share/qemu/edk2-x86_64-code.fd))

CFLAGS := \
	-target x86_64-elf \
	-std=c11 \
	-ffreestanding \
	-fno-stack-protector \
	-fno-stack-check \
	-fno-lto \
	-fno-pic \
	-fno-pie \
	-mcmodel=kernel \
	-m64 \
	-mabi=sysv \
	-mno-red-zone \
	-mno-mmx \
	-mno-sse \
	-mno-sse2 \
	-O2 \
	-Wall \
	-Wextra \
	-MMD \
	-MP \
	-Ikernel/include \
	$(EXTRA_CFLAGS)

LDFLAGS := \
	-m elf_x86_64 \
	-nostdlib \
	-static \
	-z max-page-size=0x1000 \
	-T linker/kernel.ld

USER_CFLAGS := \
	-target x86_64-elf \
	-std=c11 \
	-ffreestanding \
	-fno-stack-protector \
	-fno-stack-check \
	-fno-lto \
	-fno-pic \
	-fno-pie \
	-m64 \
	-mabi=sysv \
	-mno-red-zone \
	-mno-mmx \
	-mno-sse \
	-mno-sse2 \
	-O2 \
	-Wall \
	-Wextra \
	-MMD \
	-MP \
	-Iuserspace/lib \
	$(EXTRA_USER_CFLAGS)

USER_LDFLAGS := \
	-m elf_x86_64 \
	-nostdlib \
	-static \
	-z max-page-size=0x1000 \
	-T linker/user.ld

KERNEL_SOURCES := $(shell find kernel -name '*.c')
KERNEL_ASM_SOURCES := $(shell find kernel -name '*.S')
KERNEL_C_OBJECTS := $(patsubst kernel/%.c,$(BUILD_DIR)/kernel/%.o,$(KERNEL_SOURCES))
KERNEL_ASM_OBJECTS := $(patsubst kernel/%.S,$(BUILD_DIR)/kernel/%.o,$(KERNEL_ASM_SOURCES))
KERNEL_OBJECTS := $(KERNEL_C_OBJECTS) $(KERNEL_ASM_OBJECTS)

INIT_SOURCES := $(shell find userspace/init -name '*.c')
INIT_OBJECTS := $(patsubst userspace/%.c,$(BUILD_DIR)/userspace/%.o,$(INIT_SOURCES))
USER_LIB_SOURCES := $(shell find userspace/lib -name '*.c' 2>/dev/null)
USER_LIB_OBJECTS := $(patsubst userspace/%.c,$(BUILD_DIR)/userspace/%.o,$(USER_LIB_SOURCES))
SHELL_SOURCES := $(shell find userspace/shell -name '*.c' 2>/dev/null)
SHELL_OBJECTS := $(patsubst userspace/%.c,$(BUILD_DIR)/userspace/%.o,$(SHELL_SOURCES))
HELLO_SOURCES := $(shell find userspace/hello -name '*.c' 2>/dev/null)
HELLO_OBJECTS := $(patsubst userspace/%.c,$(BUILD_DIR)/userspace/%.o,$(HELLO_SOURCES))
LS_SOURCES := $(shell find userspace/ls -name '*.c' 2>/dev/null)
LS_OBJECTS := $(patsubst userspace/%.c,$(BUILD_DIR)/userspace/%.o,$(LS_SOURCES))
CAT_SOURCES := $(shell find userspace/cat -name '*.c' 2>/dev/null)
CAT_OBJECTS := $(patsubst userspace/%.c,$(BUILD_DIR)/userspace/%.o,$(CAT_SOURCES))
LOGIN_SOURCES := $(shell find userspace/login -name '*.c' 2>/dev/null)
LOGIN_OBJECTS := $(patsubst userspace/%.c,$(BUILD_DIR)/userspace/%.o,$(LOGIN_SOURCES))
INITRD_FILES := $(shell find initrd -type f 2>/dev/null)
INITRD_PACKED_PATHS := $(patsubst initrd/%,%,$(INITRD_FILES)) bin/shell.elf bin/hello.elf bin/ls.elf bin/cat.elf bin/login.elf

.PHONY: all kernel init shell hello ls cat initrd iso disk usb-disk usb-gpt-disk run test-exception test-page-fault test-user-fault test-user-programs test-exfat-commit clean check-tools check-uefi

all: iso

kernel: $(KERNEL)

init: $(INIT)

shell: $(USER_SHELL)

hello: $(HELLO)

ls: $(LS)

cat: $(CAT)

initrd: $(INITRD)

iso: check-tools $(ISO)

disk: $(DISK)

usb-disk: $(USB_DISK)

usb-gpt-disk: $(USB_GPT_DISK)

run: iso disk usb-disk check-uefi
	$(QEMU) \
		-M q35 \
		-m 512M \
		-cdrom $(ISO) \
		-serial stdio \
		-no-reboot \
		-no-shutdown \
		-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE) \
		-drive file=$(DISK),if=none,id=vd0,format=raw \
		-drive file=$(USB_DISK),if=none,id=usb0,format=raw \
		-device qemu-xhci,id=xhci \
		-device usb-storage,bus=xhci.0,drive=usb0 \
		-device usb-kbd,bus=xhci.0 \
		-device virtio-blk-pci,drive=vd0,disable-modern=on,disable-legacy=off

test-exception:
	$(MAKE) clean
	$(MAKE) iso EXTRA_CFLAGS=-DTANGPINGOS_TEST_EXCEPTION

test-page-fault:
	$(MAKE) clean
	$(MAKE) iso EXTRA_CFLAGS=-DTANGPINGOS_TEST_PAGE_FAULT

test-user-fault:
	$(MAKE) clean
	$(MAKE) iso EXTRA_USER_CFLAGS=-DTANGPINGOS_TEST_USER_FAULT

test-user-programs:
	$(MAKE) clean
	$(MAKE) iso EXTRA_USER_CFLAGS=-DTANGPINGOS_TEST_USER_PROGRAMS

test-exfat-commit:
	$(MAKE) clean
	$(MAKE) iso disk usb-disk EXTRA_CFLAGS=-DTANGPINGOS_TEST_EXFAT_COMMIT EXTRA_USER_CFLAGS=-DTANGPINGOS_TEST_EXFAT_COMMIT

$(KERNEL): $(KERNEL_OBJECTS) linker/kernel.ld | $(BUILD_DIR)
	$(LD) $(LDFLAGS) $(KERNEL_OBJECTS) -o $@

$(INIT): $(INIT_OBJECTS) $(USER_LIB_OBJECTS) linker/user.ld | $(BUILD_DIR)
	$(LD) $(USER_LDFLAGS) $(INIT_OBJECTS) $(USER_LIB_OBJECTS) -o $@

$(USER_SHELL): $(SHELL_OBJECTS) $(USER_LIB_OBJECTS) linker/user.ld | $(BUILD_DIR)
	$(LD) $(USER_LDFLAGS) $(SHELL_OBJECTS) $(USER_LIB_OBJECTS) -o $@

$(HELLO): $(HELLO_OBJECTS) $(USER_LIB_OBJECTS) linker/user.ld | $(BUILD_DIR)
	$(LD) $(USER_LDFLAGS) $(HELLO_OBJECTS) $(USER_LIB_OBJECTS) -o $@

$(LS): $(LS_OBJECTS) $(USER_LIB_OBJECTS) linker/user.ld | $(BUILD_DIR)
	$(LD) $(USER_LDFLAGS) $(LS_OBJECTS) $(USER_LIB_OBJECTS) -o $@

$(CAT): $(CAT_OBJECTS) $(USER_LIB_OBJECTS) linker/user.ld | $(BUILD_DIR)
	$(LD) $(USER_LDFLAGS) $(CAT_OBJECTS) $(USER_LIB_OBJECTS) -o $@

$(LOGIN): $(LOGIN_OBJECTS) $(USER_LIB_OBJECTS) linker/user.ld | $(BUILD_DIR)
	$(LD) $(USER_LDFLAGS) $(LOGIN_OBJECTS) $(USER_LIB_OBJECTS) -o $@

$(INITRD): $(INITRD_FILES) $(USER_SHELL) $(HELLO) $(LS) $(CAT) $(LOGIN) | $(BUILD_DIR)
	rm -rf $(INITRD_ROOT)
	mkdir -p $(INITRD_ROOT)/bin
	cp -R initrd/. $(INITRD_ROOT)/
	cp $(USER_SHELL) $(INITRD_ROOT)/bin/shell.elf
	cp $(HELLO) $(INITRD_ROOT)/bin/hello.elf
	cp $(LS) $(INITRD_ROOT)/bin/ls.elf
	cp $(CAT) $(INITRD_ROOT)/bin/cat.elf
	cp $(LOGIN) $(INITRD_ROOT)/bin/login.elf
	$(TAR) --format=ustar -cf $@ -C $(INITRD_ROOT) $(INITRD_PACKED_PATHS)

$(BUILD_DIR)/kernel/%.o: kernel/%.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/%.o: kernel/%.S | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) -target x86_64-elf -m64 -c $< -o $@

$(BUILD_DIR)/userspace/%.o: userspace/%.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(ISO): $(KERNEL) $(INIT) $(INITRD) boot/grub.cfg
	rm -rf $(ISO_ROOT)
	mkdir -p $(ISO_ROOT)/EFI/BOOT $(ISO_ROOT)/boot
	cp $(KERNEL) $(ISO_ROOT)/boot/kernel.elf
	cp $(INIT) $(ISO_ROOT)/boot/init.elf
	cp $(INITRD) $(ISO_ROOT)/boot/initrd.tar
	$(GRUB_MKSTANDALONE) -O x86_64-efi --fonts= --themes= --locales= \
		-d $(GRUB_MODDIR) -o $(GRUB_EFI) \
		"boot/grub/grub.cfg=boot/grub.cfg"
	rm -f $(EFI_IMG)
	dd if=/dev/zero of=$(EFI_IMG) bs=1024 count=16384
	$(MFORMAT) -i $(EFI_IMG) -v EFI ::
	$(MMD) -i $(EFI_IMG) ::EFI
	$(MMD) -i $(EFI_IMG) ::EFI/BOOT
	$(MCOPY) -i $(EFI_IMG) $(GRUB_EFI) ::EFI/BOOT/BOOTX64.EFI
	cp $(EFI_IMG) $(ISO_ROOT)/EFI/BOOT/efi.img
	$(XORRISO) -as mkisofs \
		-R \
		-r \
		-J \
		-eltorito-alt-boot \
		-e EFI/BOOT/efi.img \
		-no-emul-boot \
		--protective-msdos-label \
		$(ISO_ROOT) \
		-o $(ISO)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(DISK): | $(BUILD_DIR)
	perl scripts/make_test_disk.pl $@

$(BUILD_DIR)/usb.img: | $(BUILD_DIR)
	perl scripts/make_usb_disk.pl $@

$(BUILD_DIR)/usb-gpt.img: | $(BUILD_DIR)
	perl scripts/make_usb_disk.pl --gpt $@

check-tools:
	@command -v $(CC) >/dev/null || { echo "Missing clang. Install with: brew install llvm"; exit 1; }
	@command -v $(LD) >/dev/null || { echo "Missing ld.lld. Install with: brew install lld"; exit 1; }
	@command -v $(QEMU) >/dev/null || { echo "Missing qemu-system-x86_64. Install with: brew install qemu"; exit 1; }
	@command -v $(XORRISO) >/dev/null || { echo "Missing xorriso. Install with: brew install xorriso"; exit 1; }
	@command -v $(TAR) >/dev/null || { echo "Missing tar."; exit 1; }
	@command -v $(MFORMAT) >/dev/null || { echo "Missing mformat (mtools). Install with: brew install mtools"; exit 1; }
	@command -v $(MCOPY) >/dev/null || { echo "Missing mcopy (mtools). Install with: brew install mtools"; exit 1; }
	@command -v $(MMD) >/dev/null || { echo "Missing mmd (mtools). Install with: brew install mtools"; exit 1; }
	@[ -x $(GRUB_MKSTANDALONE) ] || { echo "Missing $(GRUB_MKSTANDALONE)"; exit 1; }
	@[ -d $(GRUB_MODDIR) ] || { echo "Missing GRUB module directory $(GRUB_MODDIR)"; exit 1; }
	@[ -f kernel/include/mb2.h ] || { echo "Missing kernel/include/mb2.h"; exit 1; }

check-uefi:
	@[ -n "$(OVMF_CODE)" ] || { \
		echo "Missing x86_64 UEFI firmware."; \
		echo "Expected one of:"; \
		echo "  $(QEMU_PREFIX)/share/qemu/edk2-x86_64-code.fd"; \
		echo "  $(BREW_PREFIX)/share/qemu/edk2-x86_64-code.fd"; \
		echo "Install or reinstall QEMU with: brew install qemu"; \
		exit 1; \
	}

clean:
	rm -rf $(BUILD_DIR)

-include $(KERNEL_C_OBJECTS:.o=.d)
-include $(INIT_OBJECTS:.o=.d)
-include $(USER_LIB_OBJECTS:.o=.d)
-include $(SHELL_OBJECTS:.o=.d)
-include $(HELLO_OBJECTS:.o=.d)
-include $(LS_OBJECTS:.o=.d)
-include $(CAT_OBJECTS:.o=.d)
