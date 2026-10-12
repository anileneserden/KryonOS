#ifndef _KERNEL_DRIVERS_USB_EHCI_H
#define _KERNEL_DRIVERS_USB_EHCI_H

#include <stdint.h>
#include <stdbool.h>

bool ehci_init(void);
uint8_t ehci_poll(void);
uint8_t ehci_mouse_active(void);

#endif