#include "disk.h"
#include "ata.h"

int disk_read_sector(uint32_t lba, void *buffer) {
    return ata_read_sector(lba, buffer);
}

int disk_write_sector(uint32_t lba, const void *buffer) {
    return ata_write_sector(lba, buffer);
}
