#include <block.h>
#include <log.h>
#include <partition.h>
#include <stdint.h>

#define MBR_SIGNATURE_OFFSET 510
#define MBR_PARTITION_TABLE_OFFSET 446
#define MBR_PARTITION_ENTRY_SIZE 16
#define MBR_PRIMARY_PARTITIONS 4
#define MBR_PARTITION_TYPE_GPT_PROTECTIVE 0xee
#define GPT_HEADER_LBA 1
#define GPT_SIGNATURE_LOW 0x5452415020494645ULL
#define GPT_MIN_HEADER_SIZE 92
#define GPT_PARTITION_ENTRY_MIN_SIZE 128
#define GPT_PARTITION_ENTRY_MAX_SIZE 512
#define GPT_MAX_ENTRIES_TO_SCAN 128
#define PARTITION_MAX_DEVICES 16

struct partition_context {
    uint64_t parent_index;
    uint64_t start_lba;
};

static struct block_device partition_devices[PARTITION_MAX_DEVICES];
static struct partition_context partition_contexts[PARTITION_MAX_DEVICES];
static uint64_t partition_count;

static uint32_t read_le32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t read_le64(const uint8_t *p) {
    return (uint64_t)read_le32(p) | ((uint64_t)read_le32(p + 4) << 32);
}

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

    if (len >= out_len) {
        return;
    }

    do {
        digits[count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value != 0 && count < sizeof(digits));

    while (count > 0 && len + 1 < out_len) {
        out[len++] = digits[--count];
    }
    out[len] = '\0';
}

static void make_partition_name(char *out, uint64_t out_len, const char *parent, uint64_t number) {
    copy_cstr_limited(out, out_len, parent);
    uint64_t len = string_length(out);

    if (len + 1 >= out_len) {
        return;
    }
    out[len++] = 'p';
    out[len] = '\0';
    append_decimal_limited(out, out_len, number);
}

static int partition_read(struct block_device *device, uint64_t lba, uint64_t count, void *buffer) {
    struct partition_context *ctx = (struct partition_context *)device->context;
    return block_read(ctx->parent_index, ctx->start_lba + lba, count, buffer);
}

static int partition_write(struct block_device *device, uint64_t lba, uint64_t count, const void *buffer) {
    struct partition_context *ctx = (struct partition_context *)device->context;
    return block_write(ctx->parent_index, ctx->start_lba + lba, count, buffer);
}

static void register_partition(uint64_t parent_index, const struct block_device_info *parent,
                               uint64_t number, uint64_t start_lba, uint64_t sector_count,
                               const char *kind) {
    if (partition_count >= PARTITION_MAX_DEVICES) {
        log_warn("%s partition table full: parent=%s part=%u\n",
                 kind, parent->name, number);
        return;
    }
    if (sector_count == 0 ||
        start_lba >= parent->block_count ||
        sector_count > parent->block_count - start_lba) {
        log_warn("%s partition outside disk: parent=%s part=%u start=%u count=%u\n",
                 kind,
                 parent->name, number, (uint64_t)start_lba, (uint64_t)sector_count);
        return;
    }

    struct partition_context *ctx = &partition_contexts[partition_count];
    struct block_device *device = &partition_devices[partition_count];
    ctx->parent_index = parent_index;
    ctx->start_lba = start_lba;

    make_partition_name(device->name, sizeof(device->name), parent->name, number);
    device->block_size = parent->block_size;
    device->block_count = sector_count;
    device->writable = parent->writable;
    device->context = ctx;
    device->read = partition_read;
    device->write = partition_write;

    if (block_register(device) == 0) {
        partition_count++;
        log_info("%s partition registered: %s start=%u sectors=%u writable=%u\n",
                 kind, device->name, start_lba, sector_count, device->writable);
    }
}

static void register_mbr_partition(uint64_t parent_index, const struct block_device_info *parent,
                                   uint64_t number, uint8_t type, uint32_t start_lba,
                                   uint32_t sector_count) {
    if (type == 0 || sector_count == 0) {
        return;
    }
    log_info("MBR partition entry: parent=%s part=%u type=%x start=%u sectors=%u\n",
             parent->name, number, (uint64_t)type, (uint64_t)start_lba,
             (uint64_t)sector_count);
    register_partition(parent_index, parent, number, start_lba, sector_count, "MBR");
}

static int guid_is_zero(const uint8_t *guid) {
    for (uint64_t i = 0; i < 16; i++) {
        if (guid[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static int scan_gpt(uint64_t parent_index, const struct block_device_info *parent) {
    uint8_t header[512];
    uint8_t entry_sector[512];
    uint64_t entry_lba;
    uint64_t entry_count;
    uint64_t entry_size;
    uint64_t first_usable;
    uint64_t last_usable;
    uint64_t scan_count;
    uint64_t registered = 0;

    if (parent->block_size != 512 || parent->block_count <= GPT_HEADER_LBA ||
        block_read(parent_index, GPT_HEADER_LBA, 1, header) != 0) {
        return -1;
    }
    if (read_le64(header) != GPT_SIGNATURE_LOW) {
        return -1;
    }

    uint32_t header_size = read_le32(header + 12);
    if (header_size < GPT_MIN_HEADER_SIZE || header_size > 512) {
        log_warn("GPT header invalid: parent=%s header_size=%u\n",
                 parent->name, (uint64_t)header_size);
        return -1;
    }

    first_usable = read_le64(header + 40);
    last_usable = read_le64(header + 48);
    entry_lba = read_le64(header + 72);
    entry_count = read_le32(header + 80);
    entry_size = read_le32(header + 84);

    log_info("GPT header: parent=%s entries_lba=%u entries=%u entry_size=%u usable=%u..%u\n",
             parent->name, entry_lba, entry_count, entry_size, first_usable, last_usable);

    if (entry_lba == 0 ||
        entry_lba >= parent->block_count ||
        entry_size < GPT_PARTITION_ENTRY_MIN_SIZE ||
        entry_size > GPT_PARTITION_ENTRY_MAX_SIZE ||
        entry_count == 0 ||
        first_usable > last_usable ||
        last_usable >= parent->block_count) {
        log_warn("GPT geometry invalid: parent=%s\n", parent->name);
        return -1;
    }

    scan_count = entry_count;
    if (scan_count > GPT_MAX_ENTRIES_TO_SCAN) {
        scan_count = GPT_MAX_ENTRIES_TO_SCAN;
    }

    for (uint64_t i = 0; i < scan_count; i++) {
        uint64_t byte_offset = i * entry_size;
        uint64_t lba = entry_lba + byte_offset / 512;
        uint64_t offset = byte_offset % 512;

        if (lba >= parent->block_count) {
            break;
        }
        if (offset + entry_size > 512) {
            log_warn("GPT entry spans sector: parent=%s part=%u entry_size=%u\n",
                     parent->name, i + 1, entry_size);
            continue;
        }
        if (block_read(parent_index, lba, 1, entry_sector) != 0) {
            log_warn("GPT entry read failed: parent=%s lba=%u\n", parent->name, lba);
            continue;
        }

        const uint8_t *entry = entry_sector + offset;
        if (guid_is_zero(entry)) {
            continue;
        }

        uint64_t first_lba = read_le64(entry + 32);
        uint64_t last_lba = read_le64(entry + 40);
        uint64_t sector_count;
        uint64_t type_guid_low = read_le64(entry);
        uint64_t type_guid_high = read_le64(entry + 8);

        if (first_lba > last_lba) {
            log_warn("GPT partition invalid: parent=%s part=%u first=%u last=%u\n",
                     parent->name, i + 1, first_lba, last_lba);
            continue;
        }
        sector_count = last_lba - first_lba + 1;
        log_info("GPT partition entry: parent=%s part=%u type=%x/%x start=%u sectors=%u\n",
                 parent->name, i + 1, type_guid_low, type_guid_high,
                 first_lba, sector_count);
        register_partition(parent_index, parent, i + 1, first_lba, sector_count, "GPT");
        registered++;
    }

    return registered > 0 ? 0 : -1;
}

static void scan_mbr(uint64_t parent_index) {
    uint8_t sector[512];
    struct block_device_info parent;
    int protective_gpt = 0;

    if (block_get_info(parent_index, &parent) != 0 || parent.block_size != 512) {
        return;
    }
    if (block_read(parent_index, 0, 1, sector) != 0) {
        return;
    }
    if (sector[MBR_SIGNATURE_OFFSET] != 0x55 || sector[MBR_SIGNATURE_OFFSET + 1] != 0xaa) {
        return;
    }

    for (uint64_t i = 0; i < MBR_PRIMARY_PARTITIONS; i++) {
        const uint8_t *entry = sector + MBR_PARTITION_TABLE_OFFSET + i * MBR_PARTITION_ENTRY_SIZE;
        uint8_t type = entry[4];
        if (type == MBR_PARTITION_TYPE_GPT_PROTECTIVE) {
            protective_gpt = 1;
        }
    }

    if (protective_gpt) {
        if (scan_gpt(parent_index, &parent) == 0) {
            return;
        }
        log_warn("protective MBR found but GPT scan failed: parent=%s\n", parent.name);
    }

    for (uint64_t i = 0; i < MBR_PRIMARY_PARTITIONS; i++) {
        const uint8_t *entry = sector + MBR_PARTITION_TABLE_OFFSET + i * MBR_PARTITION_ENTRY_SIZE;
        uint8_t type = entry[4];
        uint32_t start_lba = read_le32(entry + 8);
        uint32_t sector_count = read_le32(entry + 12);
        if (type == MBR_PARTITION_TYPE_GPT_PROTECTIVE) {
            log_info("MBR protective GPT entry skipped: parent=%s part=%u start=%u sectors=%u\n",
                     parent.name, i + 1, (uint64_t)start_lba, (uint64_t)sector_count);
            continue;
        }
        register_mbr_partition(parent_index, &parent, i + 1, type, start_lba, sector_count);
    }
}

void partition_init(void) {
    uint64_t initial_count = block_device_count();

    for (uint64_t i = 0; i < initial_count; i++) {
        scan_mbr(i);
    }
}

void partition_scan_device(uint64_t block_index) {
    scan_mbr(block_index);
}
