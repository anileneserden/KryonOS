#ifndef KERNEL_DRIVERS_USB_XHCI_H
#define KERNEL_DRIVERS_USB_XHCI_H

#include <stdint.h>

void xhci_init(void);
int xhci_detect_controller(void);
uint8_t xhci_poll(void);

#endif