#include <kernel/drivers/pci.h>
#include <kernel/serial.h>
#include <arch/x86/io.h>

#define MAX_PCI_DEVICES 32
static pci_device_t pci_devices[MAX_PCI_DEVICES];
static uint32_t pci_device_count = 0;

// --- Low-Level Config Read/Write ---

uint32_t pci_read_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) |
                       (func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_read_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t val = pci_read_config32(bus, slot, func, offset);
    return (uint16_t)((val >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_read_config8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t val = pci_read_config32(bus, slot, func, offset);
    return (uint8_t)((val >> ((offset & 3) * 8)) & 0xFF);
}

void pci_write_config32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) |
                       (func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, val);
}

void pci_write_config16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t val) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) |
                       (func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl(PCI_CONFIG_ADDRESS, address);
    
    uint32_t old_val = inl(PCI_CONFIG_DATA);
    uint32_t shift = (offset & 2) * 8;
    uint32_t mask = 0xFFFF << shift;
    uint32_t new_val = (old_val & ~mask) | ((uint32_t)val << shift);
    
    outl(PCI_CONFIG_DATA, new_val);
}

// --- PCI Control Permissions ---

void pci_enable_bus_mastering(pci_device_t* dev) {
    if (!dev) return;
    uint16_t cmd = pci_read_config16(dev->bus, dev->slot, dev->func, 0x04);
    cmd |= (PCI_COMMAND_MASTER | PCI_COMMAND_IO | PCI_COMMAND_MEMORY);
    pci_write_config16(dev->bus, dev->slot, dev->func, 0x04, cmd);
    serial_write("PCI: Bus Master and I/O permissions enabled.\n");
}

// --- PCI Device Search ---

pci_device_t* pci_get_device(uint16_t vendor_id, uint16_t device_id) {
    for (uint32_t i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].vendor_id == vendor_id && pci_devices[i].device_id == device_id) {
            return &pci_devices[i];
        }
    }
    return 0;
}

// Find PCI device by class, subclass, and prog_if
pci_device_t* pci_get_device_by_class(uint8_t class_code, uint8_t subclass, uint8_t prog_if) {
    for (uint32_t i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].class_code == class_code && 
            pci_devices[i].subclass == subclass && 
            pci_devices[i].prog_if == prog_if) {
            return &pci_devices[i];
        }
    }
    return 0;
}

// --- PCI Bus Scan ---

static void pci_check_function(uint8_t bus, uint8_t slot, uint8_t func) {
    uint16_t vendor_id = pci_read_config16(bus, slot, func, 0x00);
    if (vendor_id == 0xFFFF) return;

    if (pci_device_count >= MAX_PCI_DEVICES) return;

    pci_device_t* dev = &pci_devices[pci_device_count++];
    dev->bus = bus;
    dev->slot = slot;
    dev->func = func;
    dev->vendor_id = vendor_id;
    dev->device_id = pci_read_config16(bus, slot, func, 0x02);
    dev->class_code = pci_read_config8(bus, slot, func, 0x0B);
    dev->subclass   = pci_read_config8(bus, slot, func, 0x0A);
    dev->prog_if     = pci_read_config8(bus, slot, func, 0x09);
    dev->irq         = pci_read_config8(bus, slot, func, 0x3C);

    for (int i = 0; i < 6; i++) {
        dev->bar[i] = pci_read_config32(bus, slot, func, 0x10 + (i * 4));
    }

    serial_write("PCI: Device detected.\n");
}

void pci_init(void) {
    pci_device_count = 0;
    serial_write("PCI Bus Subsystem starting...\n");

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint16_t vendor_id = pci_read_config16(bus, slot, 0, 0x00);
            if (vendor_id == 0xFFFF) continue;

            pci_check_function(bus, slot, 0);

            uint8_t header_type = pci_read_config8(bus, slot, 0, 0x0E);
            if (header_type & PCI_HEADER_TYPE_MULTIFUNCTION) {
                for (uint8_t func = 1; func < 8; func++) {
                    if (pci_read_config16(bus, slot, func, 0x00) != 0xFFFF) {
                        pci_check_function(bus, slot, func);
                    }
                }
            }
        }
    }

    serial_write("PCI scan complete.\n");
}