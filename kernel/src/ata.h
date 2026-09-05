#ifndef ATA_H
#define ATA_H

#include <stdint.h>


void ata_init(void);
int ata_read_sector(uint32_t lba, void *buffer);
int ata_write_sector(uint32_t lba, const void *buffer);
extern uint8_t g_last_ata_status;

#endif
