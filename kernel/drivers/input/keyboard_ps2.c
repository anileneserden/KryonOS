#include <kernel/drivers/input/keyboard_ps2.h>
#include <kernel/serial.h>
#include <arch/x86/io.h>
#include <stdbool.h>

#define PS2_DATA_PORT 0x60

static bool keyboard_debug_mode = true;
static bool keyboard_log_enabled = false; // Karakterlerin serial'a yazılmasını açıp kapatmak için yeni bool

static const char* scancode_utf8[128] = {
    [0x01] = "[ESC]",
    [0x02] = "1", [0x03] = "2", [0x04] = "3", [0x05] = "4", 
    [0x06] = "5", [0x07] = "6", [0x08] = "7", [0x09] = "8", 
    [0x0A] = "9", [0x0B] = "0", [0x0C] = "*", [0x0D] = "-", 
    [0x0E] = "[BACKSPACE]",
    [0x0F] = "[TAB]",    
    [0x10] = "q", [0x11] = "w", [0x12] = "e", [0x13] = "r", 
    [0x14] = "t", [0x15] = "y", [0x16] = "u", [0x17] = "\xc4\xb1", 
    [0x18] = "o", [0x19] = "p", [0x1A] = "\xc4\x9f", [0x1B] = "\xc3\xbc",
    [0x1C] = "[ENTER]",
    [0x3A] = "[CAPSLOCK]", [0x1E] = "a", [0x1F] = "s", [0x20] = "d", [0x21] = "f", 
    [0x22] = "g", [0x23] = "h", [0x24] = "j", [0x25] = "k", 
    [0x26] = "l", [0x27] = "\xc5\x9f", [0x28] = "i",
    [0x29] = "\"",       
    [0x2B] = ",",        
    [0x2A] = "[LSHIFT]", [0x2C] = "z", [0x2D] = "x", [0x2E] = "c", [0x2F] = "v", 
    [0x30] = "b", [0x31] = "n", [0x32] = "m", 
    [0x33] = "\xc3\xb6", 
    [0x34] = "\xc3\xa7", 
    [0x35] = ".",        
    [0x36] = "[RSHIFT]",
    [0x1D] = "[LCTRL]",
    [0x5B] = "[SUPER]",
    [0x38] = "[ALT]",
    [0x39] = "[SPACE]",
    
    // Fonksiyon Tuşları (F1 - F12)
    [0x3B] = "[F1]",
    [0x3C] = "[F2]",
    [0x3D] = "[F3]",
    [0x3E] = "[F4]",
    [0x3F] = "[F5]",
    [0x40] = "[F6]",
    [0x41] = "[F7]",
    [0x42] = "[F8]",
    [0x43] = "[F9]",
    [0x44] = "[F10]",
    [0x57] = "[F11]",
    [0x58] = "[F12]"
};

static void print_hex(uint8_t val) {
    char hex[] = "0123456789ABCDEF";
    char buf[8];
    buf[0] = '[';
    buf[1] = '0';
    buf[2] = 'x';
    buf[3] = hex[(val >> 4) & 0x0F];
    buf[4] = hex[val & 0x0F];
    buf[5] = ']';
    buf[6] = ' ';
    buf[7] = '\0';
    serial_write(buf);
}

void keyboard_init(void) {
    serial_write("PS/2 Klavye surucusu baslatildi.\n");
}

void keyboard_handler(void) {
    uint8_t scancode = inb(PS2_DATA_PORT);

    if (scancode & 0x80) {
        return; 
    }

    if (scancode < 128) {
        if (keyboard_debug_mode) {
            print_hex(scancode);
        } else if (keyboard_log_enabled) {
            const char* str = scancode_utf8[scancode];
            if (str != 0) {
                serial_write(str);
            }
        }
    }
}