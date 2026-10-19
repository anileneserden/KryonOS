#include <kernel/drivers/net/e1000.h>
#include <kernel/drivers/pci.h>
#include <kernel/serial.h>
#include <kernel/mem/heap.h>
#include <stdint.h>
#include <stdbool.h>

#define E1000_NUM_RX_DESC 32
#define E1000_NUM_TX_DESC 32

// RX Descriptor Structure
typedef struct {
    uint64_t address;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed)) e1000_rx_desc_t;

// TX Descriptor Structure
typedef struct {
    uint64_t address;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed)) e1000_tx_desc_t;

static uint32_t e1000_mmio_base = 0;
static uint8_t mac_address[6];

static e1000_rx_desc_t* rx_descs = NULL;
static e1000_tx_desc_t* tx_descs = NULL;

// MMIO Read and Write Helpers
static inline void e1000_write32(uint32_t offset, uint32_t val) {
    *((volatile uint32_t *)(e1000_mmio_base + offset)) = val;
}

static inline uint32_t e1000_read32(uint32_t offset) {
    return *((volatile uint32_t *)(e1000_mmio_base + offset));
}

// EEPROM Read Helper
static uint16_t e1000_read_eeprom(uint8_t addr) {
    uint32_t val = 0;
    e1000_write32(E1000_REG_EERD, (1) | ((uint32_t)(addr) << 8));
    while (!((val = e1000_read32(E1000_REG_EERD)) & (1 << 4)));
    return (uint16_t)((val >> 16) & 0xFFFF);
}

// Read and Log MAC Address
static void e1000_read_mac(void) {
    uint32_t ral = e1000_read32(E1000_REG_RAL);
    uint32_t rah = e1000_read32(E1000_REG_RAH);

    mac_address[0] = (ral >> 0) & 0xFF;
    mac_address[1] = (ral >> 8) & 0xFF;
    mac_address[2] = (ral >> 16) & 0xFF;
    mac_address[3] = (ral >> 24) & 0xFF;
    mac_address[4] = (rah >> 0) & 0xFF;
    mac_address[5] = (rah >> 8) & 0xFF;

    if (mac_address[0] == 0 && mac_address[1] == 0 && mac_address[2] == 0) {
        uint16_t term0 = e1000_read_eeprom(0);
        uint16_t term1 = e1000_read_eeprom(1);
        uint16_t term2 = e1000_read_eeprom(2);

        mac_address[0] = term0 & 0xFF;
        mac_address[1] = term0 >> 8;
        mac_address[2] = term1 & 0xFF;
        mac_address[3] = term1 >> 8;
        mac_address[4] = term2 & 0xFF;
        mac_address[5] = term2 >> 8;
    }

    serial_write("[E1000] MAC Address: ");
    for (int i = 0; i < 6; i++) {
        serial_write_hex8(mac_address[i]);
        if (i < 5) serial_write(":");
    }
    serial_write("\n");
}

// Initialize RX and TX Descriptor Rings
static void e1000_init_rx_tx(void) {
    rx_descs = (e1000_rx_desc_t*)kzalloc(sizeof(e1000_rx_desc_t) * E1000_NUM_RX_DESC);
    tx_descs = (e1000_tx_desc_t*)kzalloc(sizeof(e1000_tx_desc_t) * E1000_NUM_TX_DESC);

    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        rx_descs[i].address = (uint64_t)(uint32_t)kzalloc(8192);
        rx_descs[i].status = 0;
    }

    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        tx_descs[i].address = 0;
        tx_descs[i].status = 1;
        tx_descs[i].cmd = 0;
    }

    // RX Register Setup
    e1000_write32(0x2800, (uint32_t)(uint32_t)rx_descs);
    e1000_write32(0x2804, 0);
    e1000_write32(0x2808, E1000_NUM_RX_DESC * sizeof(e1000_rx_desc_t));
    e1000_write32(0x2810, 0);
    e1000_write32(0x2818, E1000_NUM_RX_DESC - 1);

    e1000_write32(E1000_REG_RCTL, (1 << 1) | (1 << 3) | (1 << 4) | (1 << 26));

    // TX Register Setup
    e1000_write32(0x3800, (uint32_t)(uint32_t)tx_descs);
    e1000_write32(0x3804, 0);
    e1000_write32(0x3808, E1000_NUM_TX_DESC * sizeof(e1000_tx_desc_t));
    e1000_write32(0x3810, 0);
    e1000_write32(0x3818, 0);

    e1000_write32(E1000_REG_TCTL, (1 << 1) | (1 << 3) | (1 << 31));

    serial_write("[E1000] RX/TX rings and descriptors initialized successfully.\n");
}

void e1000_init(void) {
    serial_write("[E1000] Searching for Intel E1000 network card...\n");

    pci_device_t* dev = pci_get_device(E1000_VENDOR_ID, E1000_DEVICE_ID);
    if (!dev) {
        serial_write("[E1000] ERROR: Intel E1000 network card (0x100E) not found on QEMU!\n");
        return;
    }

    e1000_mmio_base = dev->bar[0] & ~0xF;

    serial_write("[E1000] Device found! MMIO Address: 0x");
    serial_write_hex16(e1000_mmio_base >> 16);
    serial_write_hex16(e1000_mmio_base & 0xFFFF);
    serial_write("\n");

    pci_enable_bus_mastering(dev);

    e1000_write32(E1000_REG_IMS, 0x01F60000); 
    e1000_read32(E1000_REG_ICR);

    e1000_read_mac();
    e1000_init_rx_tx();

    serial_write("[E1000] Driver successfully initialized and activated.\n");
}