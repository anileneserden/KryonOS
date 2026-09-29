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
#define ATA_CMD_CACHE_FLUSH 0xE7

#define ATA_SR_BSY          0x80    // Busy
#define ATA_SR_DRDY         0x40    // Drive ready
#define ATA_SR_DRQ          0x08    // Data request ready
#define ATA_SR_ERR          0x01    // Error flag

static void ata_400ns_delay(void) {
    for (int i = 0; i < 4; i++) {
        inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    }
}

static int ata_wait_busy(void) {
    uint32_t timeout = 100000;
    while ((inb(ATA_PRIMARY_IO + ATA_REG_STATUS) & ATA_SR_BSY) && --timeout);
    return (timeout > 0) ? 0 : -1;
}

static int ata_wait_drq(void) {
    uint32_t timeout = 100000;
    uint8_t status;
    while (--timeout) {
        status = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (status & ATA_SR_ERR) return -1;
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) return 0;
    }
    return -1;
}

void ata_init(void) {
    serial_write("ATA PIO driver starting...\n");
    
    // Select the master drive and wait
    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, 0xA0);
    ata_400ns_delay();
    ata_wait_busy();

    // Select the slave drive and wait
    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, 0xB0);
    ata_400ns_delay();
    ata_wait_busy();

    serial_write("ATA PIO driver ready (Master & Slave supported).\n");
}

void ata_read_sector(uint8_t drive, uint32_t lba, uint8_t* buf) {
    uint8_t dev_head = (drive == 0 ? 0xE0 : 0xF0) | ((lba >> 24) & 0x0F);
    
    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, dev_head);
    outb(ATA_PRIMARY_IO + ATA_REG_ERROR, 0x00);
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT, 1);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_LOW, (uint8_t) lba);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    ata_400ns_delay();

    if (ata_wait_drq() != 0) {
        serial_write("[ATA ERROR] Read DRQ timeout or error!\n");
        return;
    }

    insl(ATA_PRIMARY_IO + ATA_REG_DATA, buf, 128);
}

void ata_write_sector(uint8_t drive, uint32_t lba, const uint8_t* buf) {
    uint8_t dev_head = (drive == 0 ? 0xE0 : 0xF0) | ((lba >> 24) & 0x0F);

    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, dev_head);
    outb(ATA_PRIMARY_IO + ATA_REG_ERROR, 0x00);
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT, 1);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_LOW, (uint8_t) lba);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

    ata_400ns_delay();

    if (ata_wait_drq() != 0) {
        serial_write("[ATA ERROR] Write DRQ timeout or error!\n");
        return;
    }

    // Write 512 bytes (128 dwords)
    outsl(ATA_PRIMARY_IO + ATA_REG_DATA, buf, 128);

    // Cache Flush to ensure data is written
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    ata_400ns_delay();
    ata_wait_busy();
}

void ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, uint8_t* buf) {
    for (uint8_t i = 0; i < count; i++) {
        ata_read_sector(drive, lba + i, buf + (i * 512));
    }
}

void ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const uint8_t* buf) {
    for (uint8_t i = 0; i < count; i++) {
        ata_write_sector(drive, lba + i, buf + (i * 512));
    }
}