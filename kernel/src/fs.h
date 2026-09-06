#ifndef FS_H
#define FS_H

#include <stdint.h>

#define FS_MAGIC 0x494E4653 // "INFS" in hex, our filesystem's signature
#define MAX_FILES 32
#define MAX_FILENAME_LEN 28
#define DIRECTORY_SECTOR 1
// DATA_START_SECTOR is computed in fs.c, since it depends on how many
// sectors the directory table actually occupies.

struct superblock {
    uint32_t magic;
    uint32_t file_count;
};

struct file_entry {
    char filename[MAX_FILENAME_LEN];
    uint32_t start_sector;
    uint32_t size_bytes;
    uint8_t in_use; // 0 = empty slot, 1 = occupied
};

void fs_init(void);
int fs_format(void); // wipes and creates a fresh filesystem
int fs_create_file(const char *filename);
int fs_write_file(const char *filename, const void *data, uint32_t size);
int fs_read_file(const char *filename, void *buffer, uint32_t max_size);
int fs_list_files(struct file_entry *out_entries, int max_entries);

#endif
