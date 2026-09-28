#ifndef E1000_H
#define E1000_H

#include <stdint.h>

// Intel e1000 Vendor & Device ID
#define E1000_VENDOR_ID 0x8086
#define E1000_DEVICE_ID 0x100E

// e1000 Register Offsets (MMIO)
#define E1000_REG_CTRL    0x00000 // Device Control
#define E1000_REG_STATUS  0x00008 // Device Status
#define E1000_REG_EERD    0x00014 // EEPROM Read Register
#define E1000_REG_RAL     0x05400 // Receive Address Low
#define E1000_REG_RAH     0x05404 // Receive Address High

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint32_t mmio_base;
    uint8_t mac_addr[6];
    uint8_t has_eeprom;
} e1000_device_t;

int e1000_init(void);

#endif