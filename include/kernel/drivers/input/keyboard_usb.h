#ifndef KERNEL_DRIVERS_INPUT_KEYBOARD_USB_H
#define KERNEL_DRIVERS_INPUT_KEYBOARD_USB_H

#include <stdint.h>

void usb_keyboard_process_report(const uint8_t* report, uint8_t length);

#endif