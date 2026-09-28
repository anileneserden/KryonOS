#ifndef E1000_H
#define E1000_H

#include <stdint.h>

// Intel e1000 Vendor & Device ID
#define E1000_VENDOR_ID 0x8086
#define E1000_DEVICE_ID 0x100E

// Ring Buffer Boyutları (16 adet descriptor)
#define E1000_NUM_RX_DESC 16
#define E1000_NUM_TX_DESC 16
#define E1000_DEFAULT_PACKET_SIZE 2048

// e1000 Register Offsets (MMIO)
#define E1000_REG_CTRL    0x00000 // Device Control
#define E1000_REG_STATUS  0x00008 // Device Status
#define E1000_REG_EERD    0x00014 // EEPROM Read Register
#define E1000_REG_RCTL    0x00100 // Receive Control
#define E1000_REG_RDBAL   0x02800 // RX Descriptor Base Address Low
#define E1000_REG_RDBAH   0x02804 // RX Descriptor Base Address High
#define E1000_REG_RDLEN   0x02808 // RX Descriptor Length
#define E1000_REG_RDH     0x02810 // RX Descriptor Head
#define E1000_REG_RDT     0x02818 // RX Descriptor Tail
#define E1000_REG_TCTL    0x00400 // Transmit Control
#define E1000_REG_TDBAL   0x03800 // TX Descriptor Base Address Low
#define E1000_REG_TDBAH   0x03804 // TX Descriptor Base Address High
#define E1000_REG_TDLEN   0x03808 // TX Descriptor Length
#define E1000_REG_TDH     0x03810 // TX Descriptor Head
#define E1000_REG_TDT     0x03818 // TX Descriptor Tail
#define E1000_REG_RAL     0x05400 // Receive Address Low
#define E1000_REG_RAH     0x05404 // Receive Address High

// RX Descriptor Yapısı (Hardware Layout)
struct e1000_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed));

// TX Descriptor Yapısı (Hardware Layout)
struct e1000_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed));

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint32_t mmio_base;
    uint8_t mac_addr[6];
    uint8_t has_eeprom;

    // RX Ring
    struct e1000_rx_desc *rx_descs;
    uint16_t rx_cur;

    // TX Ring
    struct e1000_tx_desc *tx_descs;
    uint16_t tx_cur;
} e1000_device_t;

int e1000_init(void);

#endif