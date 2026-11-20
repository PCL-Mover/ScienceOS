#include <block.h>
#include <blockfs.h>
#include <console.h>
#include <devfs.h>
#include <initrd.h>
#include <mb2.h>
#include <memory.h>
#include <pci.h>
#include <partition.h>
#include <ramfs.h>
#include <scheduler.h>
#include <sysinfo.h>
#include <stdint.h>
#include <user.h>
#include <virtio_blk.h>
#include <vfs.h>
#include <x86_64/arch.h>

extern char __kernel_phys_start[];
extern char __kernel_phys_end[];

static void copy_cstr_limited(char *dst, uint64_t dst_len, const char *src) {
    uint64_t i = 0;

    if (dst_len == 0) {
        return;
    }
    while (src[i] != '\0' && i + 1 < dst_len) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static uint64_t string_length(const char *s) {
    uint64_t len = 0;

    while (s[len] != '\0') {
        len++;
    }
    return len;
}

static void append_decimal_limited(char *out, uint64_t out_len, uint64_t value) {
    char digits[20];
    uint64_t len = string_length(out);
    uint64_t count = 0;

    do {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value != 0 && count < sizeof(digits));

    while (count > 0 && len + 1 < out_len) {
        out[len++] = digits[--count];
    }
    if (len < out_len) {
        out[len] = '\0';
    }
}

static void make_usb_partition_name(char *out, uint64_t out_len, uint64_t number) {
    copy_cstr_limited(out, out_len, "usb0p");
    append_decimal_limited(out, out_len, number);
}

static void select_usb_blockfs_partitions(uint64_t *exfat_partition_index,
                                          uint64_t *fat32_partition_index) {
    if (exfat_partition_index == 0 || fat32_partition_index == 0) {
        return;
    }

    for (uint64_t part = 1; part <= 16; part++) {
        char name[32];
        uint64_t index;
        int fs_type;

        make_usb_partition_name(name, sizeof(name), part);
        if (block_find_by_name(name, &index) != 0) {
            continue;
        }

        fs_type = blockfs_detect(index);
        if (fs_type == BLOCKFS_DETECT_EXFAT &&
            *exfat_partition_index == UINT64_MAX) {
            *exfat_partition_index = index;
        } else if (fs_type == BLOCKFS_DETECT_FAT32 &&
                   *fat32_partition_index == UINT64_MAX) {
            *fat32_partition_index = index;
        }

        if (*exfat_partition_index != UINT64_MAX &&
            *fat32_partition_index != UINT64_MAX) {
            return;
        }
    }
}

static void boot_status(const char *message) {
    console_set_framebuffer_enabled(1);
    console_write(message);
    console_write("\n");
    console_set_framebuffer_enabled(0);
}

static void seed_account_database(void) {
    /* passwd: name:uid:gid   shadow: name:salt:sha256(salt+password)   group: name:x:gid: */
    static const char passwd[] = "root:0:0\n";
    static const char shadow[] =
        "root:tp0salt1:2c4149632f84ad012c21f242958a12cd4000646b0b706409e8fe8ef9bffa1da7\n";
    static const char group[] = "root:x:0:\n";

    static const char *const paths[] = {"/etc/passwd", "/etc/shadow", "/etc/group"};
    static const char *const contents[] = {passwd, shadow, group};
    static const uint64_t lengths[] = {
        sizeof(passwd) - 1, sizeof(shadow) - 1, sizeof(group) - 1,
    };

    for (uint64_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        if (vfs_file_exists(paths[i])) {
            continue;
        }
        vfs_write_file(paths[i], 0, contents[i], lengths[i]);
    }
}

static void mount_named_blockfs(const char *device_name, const char *mount_path) {
    uint64_t index;

    if (block_find_by_name(device_name, &index) == 0 &&
        blockfs_mount(index, mount_path) != 0) {
        console_write("[warn] block mount unavailable: ");
        console_write(device_name);
        console_write("\n");
    }
}

void kmain(uint32_t boot_magic, uint32_t mbi_phys) {
    console_init_serial();
    console_write("TangPingOS serial online\n");

    if (boot_info_init(boot_magic, mbi_phys) != 0) {
        kernel_panic("invalid Multiboot2 information from GRUB");
    }

    struct boot_framebuffer *fb = boot_info_framebuffer();
    if (fb == NULL) {
        kernel_panic("no framebuffer from bootloader");
    }

    console_init_framebuffer(fb);

    console_write("TangPingOS booted\n");
    console_write("UEFI framebuffer OK\n");
    console_write("framebuffer: ");
    console_write_u64(fb->width);
    console_write("x");
    console_write_u64(fb->height);
    console_write(" pitch ");
    console_write_u64(fb->pitch);
    console_write(" bpp ");
    console_write_u64(fb->bpp);
    console_write("\n");
    console_write("Starting kernel and userland...\n");
    console_write("USB probe in progress (12 second maximum)...\n");
    console_set_framebuffer_enabled(0);

    struct boot_memmap *memmap = boot_info_memmap();
    if (memmap == NULL || memmap->entry_count == 0) {
        kernel_panic("no memory map from bootloader");
    }

    console_write("memory map loaded: ");
    console_write_u64(memmap->entry_count);
    console_write(" entries\n");

    for (uint64_t i = 0; i < memmap->entry_count && i < 6; i++) {
        struct boot_memmap_entry *entry = memmap->entries[i];
        console_write("mem ");
        console_write_u64(i);
        console_write(": base ");
        console_write_hex(entry->base);
        console_write(" len ");
        console_write_hex(entry->length);
        console_write(" type ");
        console_write_u64(entry->type);
        console_write("\n");
    }

    struct boot_modules *modules = boot_info_modules();

    sysinfo_init(fb, memmap);
    memory_init(memmap, BOOT_HHDM_OFFSET);

    /*
     * Multiboot2 memory maps from GRUB are derived from firmware data and do
     * not exclude the loaded kernel, the modules or the information structure.
     * Limine used to report them as reserved, so reserve them explicitly before
     * the PMM hands those pages out.
     */
    uint64_t kernel_phys_start = (uint64_t)(uintptr_t)__kernel_phys_start;
    uint64_t kernel_phys_end = (uint64_t)(uintptr_t)__kernel_phys_end;
    memory_reserve_range(kernel_phys_start, kernel_phys_end - kernel_phys_start);
    memory_reserve_range((uint64_t)mbi_phys, boot_info_total_size());
    for (uint64_t i = 0; i < modules->module_count; i++) {
        struct boot_module *module = modules->modules[i];
        memory_reserve_range((uint64_t)(uintptr_t)module->address - BOOT_HHDM_OFFSET,
                             module->size);
    }

    console_enable_framebuffer_write_combining();
    memory_self_test();

    x86_64_interrupts_init();
    scheduler_init();
    pci_init();
    boot_status("USB probe finished; continuing startup.");
    boot_status("Initializing block devices...");
    block_init();
    struct pci_xhci_info boot_xhci;
    pci_get_xhci_info(&boot_xhci);
    if (boot_xhci.reset_policy == XHCI_RESET_POLICY_QEMU) {
        virtio_blk_init();
    }
    partition_init();
    uint64_t usb_block_index = block_device_count();
    uint64_t usb_exfat_partition_index = UINT64_MAX;
    uint64_t usb_fat32_partition_index = UINT64_MAX;
    if (pci_register_usb_storage_block_device() == 0) {
        partition_scan_device(usb_block_index);
        select_usb_blockfs_partitions(&usb_exfat_partition_index, &usb_fat32_partition_index);
    }
    boot_status("Initializing filesystems...");
    vfs_init();
    devfs_init();
    ramfs_init();
    initrd_init(modules);
    seed_account_database();
    mount_named_blockfs("vd0p1", "/usb");
    mount_named_blockfs("vd0p2", "/boot");
    if (usb_exfat_partition_index != UINT64_MAX &&
        blockfs_mount(usb_exfat_partition_index, "/usbdisk") != 0) {
        console_write("[warn] /usbdisk USB block mount unavailable\n");
    }
    if (usb_fat32_partition_index != UINT64_MAX &&
        blockfs_mount(usb_fat32_partition_index, "/usbboot") != 0) {
        console_write("[warn] /usbboot USB FAT32 mount unavailable\n");
    }
    pci_finish_usb_storage_boot_io();
    boot_status("Starting userland...");
    user_init_from_modules(modules);
    boot_status("Init task loaded; dispatching first task...");
    scheduler_dump_tasks();
    console_set_framebuffer_enabled(1);
    scheduler_dispatch_first_task();
    x86_64_interrupts_enable();

#ifdef TANGPINGOS_TEST_EXCEPTION
    console_write("triggering test exception: invalid opcode\n");
    __asm__ volatile ("ud2");
#endif

#ifdef TANGPINGOS_TEST_PAGE_FAULT
    console_write("triggering test page fault\n");
    *(volatile uint64_t *)0xffff8000dead0000ULL = 0x55;
#endif

    for (;;) {
        __asm__ volatile ("hlt");
        scheduler_dispatch_first_task();
    }
}
