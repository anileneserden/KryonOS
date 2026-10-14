#include <kernel/drivers/input/mouse_usb.h>
#include <kernel/drivers/video/fb.h>
#include <kernel/serial.h>

static uint8_t report_logged = 0;
static uint8_t absolute_mode = 0;

// USB mouse state variables
int32_t usb_mouse_x = 400;
int32_t usb_mouse_y = 300;
uint8_t usb_mouse_buttons = 0;

void usb_mouse_set_absolute_mode(uint8_t new_absolute_mode) {
    absolute_mode = new_absolute_mode;
}

uint8_t usb_mouse_is_absolute(void) {
    return absolute_mode;
}

void usb_mouse_process_report(const uint8_t* report, uint8_t length) {
    if (!report || length < 3) return;

    if (!report_logged) {
        serial_write("[xHCI] USB mouse HID report received.\n");
        report_logged = 1;
    }

    usb_mouse_buttons = report[0] & 0x07;

    if (!absolute_mode) {
        int32_t width = (int32_t)fb_get_width();
        int32_t height = (int32_t)fb_get_height();
        usb_mouse_x += (int8_t)report[1];
        usb_mouse_y += (int8_t)report[2];
        
        if (usb_mouse_x < 0) usb_mouse_x = 0;
        if (usb_mouse_y < 0) usb_mouse_y = 0;
        if (width > 0 && usb_mouse_x >= width) usb_mouse_x = width - 1;
        if (height > 0 && usb_mouse_y >= height) usb_mouse_y = height - 1;
        return;
    }

    if (length < 5) return;

    uint16_t absolute_x = (uint16_t)report[1] | ((uint16_t)report[2] << 8);
    uint16_t absolute_y = (uint16_t)report[3] | ((uint16_t)report[4] << 8);
    int32_t width = (int32_t)fb_get_width();
    int32_t height = (int32_t)fb_get_height();

    if (width > 0 && height > 0) {
        usb_mouse_x = ((int32_t)absolute_x * (width - 1)) / 32767;
        usb_mouse_y = ((int32_t)absolute_y * (height - 1)) / 32767;
    }
}