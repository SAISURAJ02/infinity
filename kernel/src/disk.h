#ifndef DISK_H
#define DISK_H

#include <stdint.h>

#define SECTOR_SIZE 512

// Generic disk interface — filesystem code should ONLY ever call these,
// never talk to ATA (or any future backend) directly.
int disk_read_sector(uint32_t lba, void *buffer);
int disk_write_sector(uint32_t lba, const void *buffer);

#endif
