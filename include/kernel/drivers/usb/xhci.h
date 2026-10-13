#ifndef KERNEL_DRIVERS_USB_XHCI_H
#define KERNEL_DRIVERS_USB_XHCI_H

#include <stdint.h>

// xHCI başlatma ve ana kontrol fonksiyonları
void xhci_init(void);
int xhci_detect_controller(void);

#endif