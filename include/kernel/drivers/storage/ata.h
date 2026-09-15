#ifndef ATA_H
#define ATA_H

#include <stdint.h>

void ata_init(void);
void ata_read_sector(uint8_t drive, uint32_t lba, uint8_t* buf);
void ata_write_sector(uint8_t drive, uint32_t lba, const uint8_t* buf);

// Çoklu sektör okuma/yazma kolaylığı için yardımcı fonksiyonlar
void ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, uint8_t* buf);
void ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const uint8_t* buf);

#endif