#ifndef KERNEL_DRIVERS_INPUT_KEYBOARD_USB_H
#define KERNEL_DRIVERS_INPUT_KEYBOARD_USB_H

#include <stdint.h>

// Takes an 8-byte HID boot keyboard report: modifiers, reserved, then up to six pressed keys.
void keyboard_usb_process_report(const uint8_t *report);

#endif
