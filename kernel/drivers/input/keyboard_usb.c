#include <kernel/drivers/input/keyboard_usb.h>
#include <kernel/drivers/input/keyboard_ps2.h>
#include <kernel/serial.h>
#include <stdbool.h>

#ifndef KEYBOARD_USB_LOG
#define KEYBOARD_USB_LOG 1
#endif

#define HID_USAGE_MAX 0x66

// HID usage ID to PS/2 Set-1 scancode. Extended keys use their base code, as the PS/2 path does.
static const uint8_t usage_to_scancode[HID_USAGE_MAX] = {
    [0x04] = 0x1E, [0x05] = 0x30, [0x06] = 0x2E, [0x07] = 0x20, [0x08] = 0x12, [0x09] = 0x21,
    [0x0A] = 0x22, [0x0B] = 0x23, [0x0C] = 0x17, [0x0D] = 0x24, [0x0E] = 0x25, [0x0F] = 0x26,
    [0x10] = 0x32, [0x11] = 0x31, [0x12] = 0x18, [0x13] = 0x19, [0x14] = 0x10, [0x15] = 0x13,
    [0x16] = 0x1F, [0x17] = 0x14, [0x18] = 0x16, [0x19] = 0x2F, [0x1A] = 0x11, [0x1B] = 0x2D,
    [0x1C] = 0x15, [0x1D] = 0x2C,
    [0x1E] = 0x02, [0x1F] = 0x03, [0x20] = 0x04, [0x21] = 0x05, [0x22] = 0x06,
    [0x23] = 0x07, [0x24] = 0x08, [0x25] = 0x09, [0x26] = 0x0A, [0x27] = 0x0B,
    [0x28] = 0x1C, [0x29] = 0x01, [0x2A] = 0x0E, [0x2B] = 0x0F, [0x2C] = 0x39,
    [0x2D] = 0x0C, [0x2E] = 0x0D, [0x2F] = 0x1A, [0x30] = 0x1B, [0x31] = 0x2B, [0x32] = 0x2B,
    [0x33] = 0x27, [0x34] = 0x28, [0x35] = 0x29, [0x36] = 0x33, [0x37] = 0x34, [0x38] = 0x35,
    [0x39] = 0x3A,
    [0x3A] = 0x3B, [0x3B] = 0x3C, [0x3C] = 0x3D, [0x3D] = 0x3E, [0x3E] = 0x3F,
    [0x3F] = 0x40, [0x40] = 0x41, [0x41] = 0x42, [0x42] = 0x43, [0x43] = 0x44,
    [0x44] = 0x57, [0x45] = 0x58,
    [0x49] = 0x52, [0x4A] = 0x47, [0x4B] = 0x49, [0x4C] = 0x53, [0x4D] = 0x4F, [0x4E] = 0x51,
    [0x4F] = 0x4D, [0x50] = 0x4B, [0x51] = 0x50, [0x52] = 0x48,
    [0x53] = 0x45,
};

// Bit order of the HID modifier byte: LCtrl, LShift, LAlt, LGui, RCtrl, RShift, RAlt, RGui.
static const uint8_t modifier_to_scancode[8] = { 0x1D, 0x2A, 0x38, 0x5B, 0x1D, 0x36, 0x38, 0x5C };

static uint8_t previous_keys[6];
static uint8_t previous_modifiers;

static bool contains(const uint8_t *keys, uint8_t usage) {
    for (int i = 0; i < 6; i++) {
        if (keys[i] == usage) return true;
    }
    return false;
}

static void emit(uint8_t usage, uint8_t scancode, bool released) {
    if (KEYBOARD_USB_LOG) {
        serial_write(released ? "[USB-KBD] release usage=0x" : "[USB-KBD] press usage=0x");
        serial_write_hex8(usage);
        serial_write(" scancode=0x");
        serial_write_hex8(scancode);
        serial_write("\n");
    }
    keyboard_process_scancode(scancode, released);
}

void keyboard_usb_process_report(const uint8_t *report) {
    const uint8_t *keys = &report[2];

    // Every slot reads 0x01 when more keys are held than the keyboard can report.
    if (keys[0] == 0x01) return;

    uint8_t modifiers = report[0];
    for (int bit = 0; bit < 8; bit++) {
        uint8_t mask = 1U << bit;
        if ((modifiers ^ previous_modifiers) & mask) {
            emit(0xE0 + bit, modifier_to_scancode[bit], !(modifiers & mask));
        }
    }

    for (int i = 0; i < 6; i++) {
        uint8_t usage = previous_keys[i];
        if (usage >= HID_USAGE_MAX || !usage_to_scancode[usage] || contains(keys, usage)) continue;
        emit(usage, usage_to_scancode[usage], true);
    }

    for (int i = 0; i < 6; i++) {
        uint8_t usage = keys[i];
        if (usage >= HID_USAGE_MAX || !usage_to_scancode[usage] || contains(previous_keys, usage)) continue;
        emit(usage, usage_to_scancode[usage], false);
    }

    for (int i = 0; i < 6; i++) previous_keys[i] = keys[i];
    previous_modifiers = modifiers;
}
