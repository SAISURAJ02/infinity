#include "ata.h"
#include "io.h"

#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_SECTOR_CNT  0x1F2
#define ATA_LBA_LOW     0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HIGH    0x1F5
#define ATA_DRIVE_HEAD  0x1F6
#define ATA_STATUS      0x1F7
#define ATA_COMMAND     0x1F7

#define ATA_CMD_READ    0x20
#define ATA_CMD_WRITE   0x30

#define ATA_STATUS_BSY  0x80
#define ATA_STATUS_DRQ  0x08
#define ATA_STATUS_ERR  0x01

void ata_init(void) {
    // Nothing to initialize yet — PIO mode works without setup on
    // most emulated/real ATA controllers, including QEMU's default.
}

uint8_t g_last_ata_status; // exposed for debugging

static int ata_wait_ready(void) {
    // Bounded polling — 100,000 attempts, so a stuck drive doesn't hang forever.
    for (int attempts = 0; attempts < 100000; attempts++) {
        uint8_t status = inb(ATA_STATUS);
        g_last_ata_status = status;
        if (!(status & ATA_STATUS_BSY)) {
            if (status & ATA_STATUS_ERR) {
                return -1;
            }
            if (status & ATA_STATUS_DRQ) {
                return 0;
            }
        }
    }
    return -2; // timed out, never became ready
}
int ata_read_sector(uint32_t lba, void *buffer) {
    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F)); // drive 0 (master), high 4 bits of LBA
    outb(ATA_SECTOR_CNT, 1);                            // read 1 sector
    outb(ATA_LBA_LOW,  (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND, ATA_CMD_READ);

    if (ata_wait_ready() != 0) {
        return -1; // drive reported an error
    }

    uint16_t *buf16 = (uint16_t *)buffer;
    for (int i = 0; i < 256; i++) {
        buf16[i] = inw(ATA_DATA); // read 256 x 16-bit words = 512 bytes
    }

    return 0;
}

int ata_write_sector(uint32_t lba, const void *buffer) {
    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_SECTOR_CNT, 1);
    outb(ATA_LBA_LOW,  (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND, ATA_CMD_WRITE);
    
    if (ata_wait_ready() !=0){
    	return -1;
    }


    const uint16_t *buf16 = (const uint16_t *)buffer;
    for (int i = 0; i < 256; i++) {
        outw(ATA_DATA, buf16[i]);
    }

    return 0;
}
