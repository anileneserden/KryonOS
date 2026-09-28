#include <kernel/drivers/net/e1000.h>
#include <kernel/drivers/pci.h>
#include <kernel/serial.h>
#include <arch/x86/io.h>

static e1000_device_t e1000_dev;

static inline void e1000_write32(uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(e1000_dev.mmio_base + reg) = val;
}

static inline uint32_t e1000_read32(uint32_t reg) {
    return *(volatile uint32_t*)(e1000_dev.mmio_base + reg);
}

static uint16_t e1000_read_eeprom(uint8_t addr) {
    uint32_t val = 0;
    e1000_write32(E1000_REG_EERD, (1 << 0) | ((uint32_t)addr << 8));
    while (!((val = e1000_read32(E1000_REG_EERD)) & (1 << 4))) {
        asm volatile("nop");
    }
    return (uint16_t)((val >> 16) & 0xFFFF);
}

static void e1000_read_mac(void) {
    e1000_write32(E1000_REG_EERD, 0x1);
    uint32_t eerd = e1000_read32(E1000_REG_EERD);
    
    if (eerd & 0x10) {
        e1000_dev.has_eeprom = 1;
        uint16_t val0 = e1000_read_eeprom(0);
        uint16_t val1 = e1000_read_eeprom(1);
        uint16_t val2 = e1000_read_eeprom(2);

        e1000_dev.mac_addr[0] = val0 & 0xFF;
        e1000_dev.mac_addr[1] = val0 >> 8;
        e1000_dev.mac_addr[2] = val1 & 0xFF;
        e1000_dev.mac_addr[3] = val1 >> 8;
        e1000_dev.mac_addr[4] = val2 & 0xFF;
        e1000_dev.mac_addr[5] = val2 >> 8;
    } else {
        e1000_dev.has_eeprom = 0;
        uint32_t ral = e1000_read32(E1000_REG_RAL);
        uint32_t rah = e1000_read32(E1000_REG_RAH);

        e1000_dev.mac_addr[0] = ral & 0xFF;
        e1000_dev.mac_addr[1] = (ral >> 8) & 0xFF;
        e1000_dev.mac_addr[2] = (ral >> 16) & 0xFF;
        e1000_dev.mac_addr[3] = (ral >> 24) & 0xFF;
        e1000_dev.mac_addr[4] = rah & 0xFF;
        e1000_dev.mac_addr[5] = (rah >> 8) & 0xFF;
    }
}

static void serial_write_hex8(uint8_t val) {
    const char hex_chars[] = "0123456789ABCDEF";
    serial_write_char(hex_chars[(val >> 4) & 0x0F]);
    serial_write_char(hex_chars[val & 0x0F]);
}

int e1000_init(void) {
    serial_write("[e1000] Looking for Intel e1000 network card in PCI registry...\n");

    pci_device_t* pci_dev = pci_get_device(E1000_VENDOR_ID, E1000_DEVICE_ID);
    if (!pci_dev) {
        serial_write("[e1000] ERROR: Intel e1000 network card not found on PCI bus!\n");
        return -1;
    }

    e1000_dev.bus = pci_dev->bus;
    e1000_dev.slot = pci_dev->slot;
    e1000_dev.func = pci_dev->func;

    serial_write("[e1000] Controller found at PCI ");
    serial_write_hex8(pci_dev->bus);
    serial_write(":");
    serial_write_hex8(pci_dev->slot);
    serial_write(".");
    serial_write_hex8(pci_dev->func);
    serial_write("\n");

    pci_enable_bus_mastering(pci_dev);
    e1000_dev.mmio_base = pci_dev->bar[0] & ~0xF;

    serial_write("[e1000] MMIO Base Address (BAR0): 0x");
    uint32_t base = e1000_dev.mmio_base;
    serial_write_hex8((base >> 24) & 0xFF);
    serial_write_hex8((base >> 16) & 0xFF);
    serial_write_hex8((base >> 8) & 0xFF);
    serial_write_hex8(base & 0xFF);
    serial_write("\n");

    e1000_read_mac();

    serial_write("[e1000] Hardware MAC Address: ");
    for (int i = 0; i < 6; i++) {
        serial_write_hex8(e1000_dev.mac_addr[i]);
        if (i < 5) serial_write(":");
    }
    serial_write("\n");

    serial_write("[e1000] Hardware discovery & MAC reading completed successfully.\n");
    return 0;
}