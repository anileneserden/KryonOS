#ifndef KERNEL_DRIVERS_STORAGE_ATA_H
#define KERNEL_DRIVERS_STORAGE_ATA_H

#include <stdint.h>

void ata_init(void);
void ata_read_sector(uint32_t lba, uint8_t* buf);
void ata_write_sector(uint32_t lba, const uint8_t* buf);

#endif