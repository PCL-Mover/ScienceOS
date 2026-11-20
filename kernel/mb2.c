#include <mb2.h>
#include <stddef.h>

#define BOOT_MAX_MEMMAP_ENTRIES 256
#define BOOT_MAX_MODULES 16

static struct boot_framebuffer framebuffer;
static struct boot_memmap_entry memmap_entries[BOOT_MAX_MEMMAP_ENTRIES];
static struct boot_memmap_entry *memmap_pointers[BOOT_MAX_MEMMAP_ENTRIES];
static struct boot_memmap memmap;
static struct boot_module modules[BOOT_MAX_MODULES];
static struct boot_module *module_pointers[BOOT_MAX_MODULES];
static struct boot_modules module_list;
static uint64_t mbi_total_size;

static void *hhdm_pointer(uint64_t phys_addr) {
    return (void *)(BOOT_HHDM_OFFSET + phys_addr);
}

static uint32_t align_tag_size(uint32_t size) {
    return (size + 7u) & ~7u;
}

int boot_info_init(uint32_t magic, uint32_t mbi_phys) {
    if (magic != MULTIBOOT2_BOOTLOADER_MAGIC || mbi_phys == 0) {
        return -1;
    }

    /*
     * The bootstrap page tables keep an identity map of the low 4 GiB active,
     * so the information structure can be walked at its physical address.
     */
    const uint8_t *base = (const uint8_t *)(uintptr_t)(uint64_t)mbi_phys;
    uint32_t total_size = *(const uint32_t *)base;
    const uint8_t *cursor = base + 8;
    const uint8_t *end = base + total_size;

    mbi_total_size = total_size;

    framebuffer.address = NULL;
    memmap.entry_count = 0;
    memmap.entries = memmap_pointers;
    module_list.module_count = 0;
    module_list.modules = module_pointers;

    while (cursor + sizeof(struct multiboot2_tag) <= end) {
        const struct multiboot2_tag *tag = (const struct multiboot2_tag *)cursor;

        if (tag->type == MULTIBOOT2_TAG_END) {
            break;
        }
        if (tag->size < sizeof(struct multiboot2_tag)) {
            break;
        }

        if (tag->type == MULTIBOOT2_TAG_MMAP) {
            const struct multiboot2_tag_mmap *mmap_tag =
                (const struct multiboot2_tag_mmap *)cursor;
            uint32_t entry_size = mmap_tag->entry_size;
            uint32_t payload = tag->size - sizeof(struct multiboot2_tag_mmap);

            if (entry_size >= sizeof(struct multiboot2_mmap_entry)) {
                const uint8_t *entry = cursor + sizeof(struct multiboot2_tag_mmap);
                while (payload >= entry_size &&
                       memmap.entry_count < BOOT_MAX_MEMMAP_ENTRIES) {
                    const struct multiboot2_mmap_entry *raw =
                        (const struct multiboot2_mmap_entry *)entry;
                    struct boot_memmap_entry *dst =
                        &memmap_entries[memmap.entry_count];

                    dst->base = raw->addr;
                    dst->length = raw->len;
                    dst->type = raw->type;
                    memmap_pointers[memmap.entry_count] = dst;
                    memmap.entry_count++;

                    entry += entry_size;
                    payload -= entry_size;
                }
            }
        } else if (tag->type == MULTIBOOT2_TAG_FRAMEBUFFER) {
            const struct multiboot2_tag_framebuffer *fb_tag =
                (const struct multiboot2_tag_framebuffer *)cursor;

            framebuffer.address = hhdm_pointer(fb_tag->framebuffer_addr);
            framebuffer.width = fb_tag->framebuffer_width;
            framebuffer.height = fb_tag->framebuffer_height;
            framebuffer.pitch = fb_tag->framebuffer_pitch;
            framebuffer.bpp = fb_tag->framebuffer_bpp;
        } else if (tag->type == MULTIBOOT2_TAG_MODULE) {
            const struct multiboot2_tag_module *mod =
                (const struct multiboot2_tag_module *)cursor;

            if (module_list.module_count < BOOT_MAX_MODULES) {
                struct boot_module *dst = &modules[module_list.module_count];
                const char *cmdline = (const char *)hhdm_pointer(
                    (uint64_t)(uintptr_t)(cursor + sizeof(struct multiboot2_tag_module)));

                dst->address = hhdm_pointer(mod->mod_start);
                dst->size = (uint64_t)mod->mod_end - (uint64_t)mod->mod_start;
                dst->path = NULL;
                dst->string = cmdline;
                module_pointers[module_list.module_count] = dst;
                module_list.module_count++;
            }
        }

        cursor += align_tag_size(tag->size);
    }

    if (memmap.entry_count == 0) {
        return -1;
    }

    return 0;
}

uint64_t boot_info_total_size(void) {
    return mbi_total_size;
}

struct boot_framebuffer *boot_info_framebuffer(void) {
    return framebuffer.address == NULL ? NULL : &framebuffer;
}

struct boot_memmap *boot_info_memmap(void) {
    return &memmap;
}

struct boot_modules *boot_info_modules(void) {
    return &module_list;
}
