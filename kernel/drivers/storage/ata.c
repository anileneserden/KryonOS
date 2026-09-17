#include <kernel/drivers/storage/ata.h>
#include <kernel/serial.h>
#include <arch/x86/io.h>

#define ATA_PRIMARY_IO      0x1F0
#define ATA_REG_DATA        0x00
#define ATA_REG_ERROR       0x01
#define ATA_REG_SECCOUNT    0x02
#define ATA_REG_LBA_LOW     0x03
#define ATA_REG_LBA_MID     0x04
#define ATA_REG_LBA_HIGH    0x05
#define ATA_REG_HDDEVSEL    0x06
#define ATA_REG_COMMAND     0x07
#define ATA_REG_STATUS      0x07

#define ATA_CMD_READ_PIO    0x20
#define ATA_CMD_WRITE_PIO   0x30

#define ATA_SR_BSY          0x80    // Busy
#define ATA_SR_DRDY         0x40    // Drive ready
#define ATA_SR_DRQ          0x08    // Data request ready

static void ata_poll(void) {
    // 400ns delay by reading 4 times (to allow the status BSY bit to settle)
    for (int i = 0; i < 4; i++) {
        inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    }
    while (inb(ATA_PRIMARY_IO + ATA_REG_STATUS) & ATA_SR_BSY);
    while (!(inb(ATA_PRIMARY_IO + ATA_REG_STATUS) & ATA_SR_DRQ));
}

void ata_init(void) {
    serial_write("ATA PIO driver starting...\n");
    
    // Select the master drive and wait
    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, 0xA0);
    for (int i = 0; i < 4; i++) {
        inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    }
    while (inb(ATA_PRIMARY_IO + ATA_REG_STATUS) & ATA_SR_BSY);

    // Select the slave drive and wait
    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, 0xB0);
    for (int i = 0; i < 4; i++) {
        inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    }
    while (inb(ATA_PRIMARY_IO + ATA_REG_STATUS) & ATA_SR_BSY);

    serial_write("ATA PIO driver ready (Master & Slave supported).\n");
}

void ata_read_sector(uint8_t drive, uint32_t lba, uint8_t* buf) {
    // drive == 0 ise 0xE0 (Master), drive == 1 ise 0xF0 (Slave)
    uint8_t dev_head = (drive == 0 ? 0xE0 : 0xF0) | ((lba >> 24) & 0x0F);
    
    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, dev_head);
    outb(ATA_PRIMARY_IO + ATA_REG_ERROR, 0x00);
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT, 1); // Read 1 sector
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_LOW, (uint8_t) lba);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    ata_poll();

    // 512 bytes = 128 pieces of 32-bit (4-byte) data
    insl(ATA_PRIMARY_IO + ATA_REG_DATA, buf, 128);
}

void ata_write_sector(uint8_t drive, uint32_t lba, const uint8_t* buf) {
    uint8_t dev_head = (drive == 0 ? 0xE0 : 0xF0) | ((lba >> 24) & 0x0F);

    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, dev_head);
    outb(ATA_PRIMARY_IO + ATA_REG_ERROR, 0x00);
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT, 1); // Write 1 sector
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_LOW, (uint8_t) lba);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

    ata_poll();

    outsl(ATA_PRIMARY_IO + ATA_REG_DATA, buf, 128);
}

// Provides the convenience of reading multiple sectors sequentially.
void ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, uint8_t* buf) {
    for (uint8_t i = 0; i < count; i++) {
        ata_read_sector(drive, lba + i, buf + (i * 512));
    }
}

// Provides the convenience of listing multiple sectors in sequence
void ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const uint8_t* buf) {
    for (uint8_t i = 0; i < count; i++) {
        ata_write_sector(drive, lba + i, buf + (i * 512));
    }
}