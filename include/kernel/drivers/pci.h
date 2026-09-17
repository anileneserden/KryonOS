#ifndef PCI_H
#define PCI_H

#include <stdint.h>

// PCI I/O ports
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

// PCI command register bit definitions
#define PCI_COMMAND_IO     0x01 // I/O Space Enable
#define PCI_COMMAND_MEMORY 0x02 // Memory Space Enable
#define PCI_COMMAND_MASTER 0x04 // Bus Master Enable

// PCI Header Tipi
#define PCI_HEADER_TYPE_MULTIFUNCTION 0x80

// PCI device structure
typedef struct {
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  irq;
    uint32_t bar[6];
} pci_device_t;

// Fonksiyon Bildirimleri
uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint8_t  pci_read_config8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

void pci_write_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
void pci_write_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val);

void pci_init(void);
pci_device_t* pci_get_device(uint16_t vendor_id, uint16_t device_id);
void pci_enable_bus_mastering(pci_device_t* dev);

#endif