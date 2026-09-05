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
    // 4 kez okuyarak 400ns gecikme (atus bsy bitinin oturması için)
    for (int i = 0; i < 4; i++) {
        inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    }
    while (inb(ATA_PRIMARY_IO + ATA_REG_STATUS) & ATA_SR_BSY);
    while (!(inb(ATA_PRIMARY_IO + ATA_REG_STATUS) & ATA_SR_DRQ));
}

void ata_init(void) {
    serial_write("ATA PIO surucusu baslatiliyor...\n");
    
    // Master sürücüyü seç
    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, 0xA0);
    
    // Sadece meşgul (BSY) bayrağının düşmesini bekle (DRQ beklemiyoruz!)
    for (int i = 0; i < 4; i++) {
        inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    }
    while (inb(ATA_PRIMARY_IO + ATA_REG_STATUS) & ATA_SR_BSY);
    
    serial_write("ATA PIO surucusu hazir.\n");
}

void ata_read_sector(uint32_t lba, uint8_t* buf) {
    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_IO + ATA_REG_ERROR, 0x00);
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT, 1); // 1 sektör oku
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_LOW, (uint8_t) lba);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    ata_poll();

    // 512 bayt = 128 adet 32-bit (4 bayt) veri
    insl(ATA_PRIMARY_IO + ATA_REG_DATA, buf, 128);
}

void ata_write_sector(uint32_t lba, const uint8_t* buf) {
    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_IO + ATA_REG_ERROR, 0x00);
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT, 1); // 1 sektör yaz
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_LOW, (uint8_t) lba);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

    ata_poll();

    outsl(ATA_PRIMARY_IO + ATA_REG_DATA, buf, 128);
}