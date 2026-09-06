#include "fs.h"
#include "disk.h"
#include <stddef.h>

#define DIRECTORY_SIZE_BYTES (sizeof(struct file_entry) * MAX_FILES)
#define DIRECTORY_SECTORS ((DIRECTORY_SIZE_BYTES + SECTOR_SIZE - 1) / SECTOR_SIZE)
#define DATA_START_SECTOR (DIRECTORY_SECTOR + DIRECTORY_SECTORS)

static struct superblock sb;
static struct file_entry directory[MAX_FILES];

static void load_directory(void) {
    uint8_t *dest = (uint8_t *)directory;
    for (uint32_t i = 0; i < DIRECTORY_SECTORS; i++) {
        disk_read_sector(DIRECTORY_SECTOR + i, dest + (i * SECTOR_SIZE));
    }
}

static void save_directory(void) {
    uint8_t *src = (uint8_t *)directory;
    for (uint32_t i = 0; i < DIRECTORY_SECTORS; i++) {
        disk_write_sector(DIRECTORY_SECTOR + i, src + (i * SECTOR_SIZE));
    }
}

int fs_format(void) {
    sb.magic = FS_MAGIC;
    sb.file_count = 0;
    disk_write_sector(0, &sb);

    for (int i = 0; i < MAX_FILES; i++) {
        directory[i].in_use = 0;
    }
    save_directory();

    return 0;
}

void fs_init(void) {
    disk_read_sector(0, &sb);

    if (sb.magic != FS_MAGIC) {
        fs_format();
    } else {
        load_directory();
    }
}
#include <stdint.h>

static int str_equal(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

static void str_copy(char *dest, const char *src, int max_len) {
    int i = 0;
    while (src[i] != '\0' && i < max_len - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

int fs_create_file(const char *filename) {
    // Check if a file with this name already exists
    for (int i = 0; i < MAX_FILES; i++) {
        if (directory[i].in_use && str_equal(directory[i].filename, filename)) {
            return -1; // already exists
        }
    }

    // Find an empty slot
    for (int i = 0; i < MAX_FILES; i++) {
        if (!directory[i].in_use) {
            str_copy(directory[i].filename, filename, MAX_FILENAME_LEN);
            directory[i].start_sector = 0; // assigned when the file is first written
            directory[i].size_bytes = 0;
            directory[i].in_use = 1;

            sb.file_count++;
            disk_write_sector(0, &sb);
            save_directory();

            return 0;
        }
    }

    return -2; // directory full
}
int fs_write_file(const char *filename, const void *data, uint32_t size) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (directory[i].in_use && str_equal(directory[i].filename, filename)) {
            uint32_t sectors_needed = (size + SECTOR_SIZE - 1) / SECTOR_SIZE;

            // SIMPLIFICATION: always place this file's data starting right
            // after the directory table, at a fixed offset based on its
            // directory slot index. This avoids needing a real free-space
            // allocator for now, at the cost of wasting space between files.
            uint32_t start_sector = DATA_START_SECTOR + (i * 64); // 64 sectors = 32KB per file slot, arbitrary but simple

            const uint8_t *src = (const uint8_t *)data;
            for (uint32_t s = 0; s < sectors_needed; s++) {
                uint8_t sector_buf[SECTOR_SIZE] = {0};
                uint32_t bytes_this_sector = size - (s * SECTOR_SIZE);
                if (bytes_this_sector > SECTOR_SIZE) bytes_this_sector = SECTOR_SIZE;

                for (uint32_t b = 0; b < bytes_this_sector; b++) {
                    sector_buf[b] = src[s * SECTOR_SIZE + b];
                }

                disk_write_sector(start_sector + s, sector_buf);
            }

            directory[i].start_sector = start_sector;
            directory[i].size_bytes = size;
            save_directory();

            return 0;
        }
    }
    return -1; // file not found
}
int fs_read_file(const char *filename, void *buffer, uint32_t max_size) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (directory[i].in_use && str_equal(directory[i].filename, filename)) {
            uint32_t size = directory[i].size_bytes;
            if (size > max_size) {
                size = max_size; // truncate if the caller's buffer is smaller
            }

            uint32_t sectors_needed = (size + SECTOR_SIZE - 1) / SECTOR_SIZE;
            uint8_t *dest = (uint8_t *)buffer;

            for (uint32_t s = 0; s < sectors_needed; s++) {
                uint8_t sector_buf[SECTOR_SIZE];
                disk_read_sector(directory[i].start_sector + s, sector_buf);

                uint32_t bytes_this_sector = size - (s * SECTOR_SIZE);
                if (bytes_this_sector > SECTOR_SIZE) bytes_this_sector = SECTOR_SIZE;

                for (uint32_t b = 0; b < bytes_this_sector; b++) {
                    dest[s * SECTOR_SIZE + b] = sector_buf[b];
                }
            }

            return (int)size; // return how many bytes were actually read
        }
    }
    return -1; // file not found
}

int fs_list_files(struct file_entry *out_entries, int max_entries) {
    int count = 0;
    for (int i = 0; i < MAX_FILES && count < max_entries; i++) {
        if (directory[i].in_use) {
            out_entries[count] = directory[i];
            count++;
        }
    }
    return count;
}
