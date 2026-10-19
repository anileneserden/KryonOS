#ifndef KERNEL_DRIVERS_NET_E1000_H
#define KERNEL_DRIVERS_NET_E1000_H

#include <stdint.h>

#define E1000_VENDOR_ID     0x8086
#define E1000_DEVICE_ID     0x100E  // QEMU'nun varsayılan Intel 82540EM cihaz kodu

// Temel E1000 Register Offsetleri
#define E1000_REG_CTRL      0x00000
#define E1000_REG_STATUS    0x00008
#define E1000_REG_EERD      0x00014
#define E1000_REG_ICR       0x000C0
#define E1000_REG_IMS       0x000D0
#define E1000_REG_RCTL      0x00100
#define E1000_REG_TCTL      0x00400
#define E1000_REG_RAL       0x05400
#define E1000_REG_RAH       0x05404

void e1000_init(void);

#endif