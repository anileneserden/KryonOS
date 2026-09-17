#ifndef KERNEL_DRIVERS_INPUT_MOUSE_USB_H
#define KERNEL_DRIVERS_INPUT_MOUSE_USB_H

#include <stdint.h>

void usb_mouse_process_report(const uint8_t* report, uint8_t length);
void usb_mouse_set_absolute_mode(uint8_t absolute_mode);

#endif
