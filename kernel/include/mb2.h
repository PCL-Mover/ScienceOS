#ifndef TANGPINGOS_MB2_H
#define TANGPINGOS_MB2_H

/*
 * Multiboot2 (GRUB) boot protocol definitions.
 *
 * GRUB loads the ELF64 kernel below the 4 GiB border and jumps to the 32-bit
 * trampoline in kernel/boot.S, which builds page tables, enters long mode and
 * calls kmain() with the boot magic in %eax and the physical address of the
 * Multiboot2 information structure in %ebx.
 */

#include <stdint.h>

#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36d76289u
#define MULTIBOOT2_HEADER_MAGIC     0xE85250D6u

/* Multiboot2 tag types. */
#define MULTIBOOT2_TAG_END         0
#define MULTIBOOT2_TAG_CMDLINE     1
#define MULTIBOOT2_TAG_BOOTLOADER  2
#define MULTIBOOT2_TAG_MODULE      3
#define MULTIBOOT2_TAG_BASIC_MEM   4
#define MULTIBOOT2_TAG_BOOTDEV     5
#define MULTIBOOT2_TAG_MMAP        6
#define MULTIBOOT2_TAG_FRAMEBUFFER 8

/* Multiboot2 memory map entry types. */
#define MULTIBOOT2_MEMORY_AVAILABLE 1
#define MULTIBOOT2_MEMORY_RESERVED  2

/* Higher-half direct map base, matching the PML4[256] entry in boot.S. */
#define BOOT_HHDM_OFFSET 0xffff800000000000ULL

struct multiboot2_tag {
    uint32_t type;
    uint32_t size;
};

struct multiboot2_tag_module {
    uint32_t type;
    uint32_t size;
    uint32_t mod_start;
    uint32_t mod_end;
};

struct multiboot2_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t zero;
} __attribute__((packed));

struct multiboot2_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
};

struct multiboot2_tag_framebuffer {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint16_t reserved;
} __attribute__((packed));

/*
 * Normalized boot information handed to the rest of the kernel.  The shapes
 * match what the drivers expect: memory map and module entries are reachable
 * through an array of pointers.
 */

struct boot_framebuffer {
    void *address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bpp;
};

struct boot_memmap_entry {
    uint64_t base;
    uint64_t length;
    uint64_t type;
};

struct boot_memmap {
    uint64_t entry_count;
    struct boot_memmap_entry **entries;
};

struct boot_module {
    void *address;
    uint64_t size;
    const char *path;
    const char *string;
};

struct boot_modules {
    uint64_t module_count;
    struct boot_module **modules;
};

/*
 * Parse the Multiboot2 information structure.  Returns 0 on success and -1 on
 * failure.  Parsed data is stored in static storage owned by mb2.c so the
 * returned pointers stay valid for the kernel's lifetime.
 */
int boot_info_init(uint32_t magic, uint32_t mbi_phys);

/* Size in bytes of the Multiboot2 information structure (valid after init). */
uint64_t boot_info_total_size(void);

struct boot_framebuffer *boot_info_framebuffer(void);
struct boot_memmap *boot_info_memmap(void);
struct boot_modules *boot_info_modules(void);

#endif
