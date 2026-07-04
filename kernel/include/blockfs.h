#ifndef TANGPINGOS_BLOCKFS_H
#define TANGPINGOS_BLOCKFS_H

#include <stdint.h>

#define BLOCKFS_DETECT_NONE 0
#define BLOCKFS_DETECT_EXFAT 1
#define BLOCKFS_DETECT_FAT32 2
#define BLOCKFS_UNLOCK_MAGIC 0x5450555342575249ULL

struct blockfs_unlock_request {
    uint64_t magic;
    uint64_t expected_bitmap_lba;
    uint64_t expected_fat_lba;
    uint64_t expected_directory_lba;
    uint64_t flags;
};

int blockfs_detect(uint64_t block_index);
int blockfs_mount(uint64_t block_index, const char *mount_path);
int blockfs_request_write_unlock(const char *mount_path,
                                 const struct blockfs_unlock_request *request);

#endif
