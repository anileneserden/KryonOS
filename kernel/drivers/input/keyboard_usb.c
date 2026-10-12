#include <kernel/drivers/input/keyboard_usb.h>
#include <kernel/serial.h>

static uint8_t previous_keys[6];
static uint8_t previous_modifiers;
static uint8_t report_logged;

static char usb_keyboard_usage_to_ascii(uint8_t usage, uint8_t shift) {
    if (usage >= 0x04 && usage <= 0x1D) {
        char letter = (char)('a' + usage - 0x04);
        return shift ? (char)(letter - 'a' + 'A') : letter;
    }
    if (usage >= 0x1E && usage <= 0x26) {
        static const char normal_digits[] = "123456789";
        static const char shifted_digits[] = "!@#$%^&*(";
        return shift ? shifted_digits[usage - 0x1E] : normal_digits[usage - 0x1E];
    }
    if (usage == 0x27) return shift ? ')' : '0';
    if (usage == 0x2C) return ' ';
    if (usage == 0x2D) return shift ? '_' : '-';
    if (usage == 0x2E) return shift ? '+' : '=';
    if (usage == 0x2F) return shift ? '{' : '[';
    if (usage == 0x30) return shift ? '}' : ']';
    if (usage == 0x31 || usage == 0x32) return shift ? '|' : '\\';
    if (usage == 0x33) return shift ? ':' : ';';
    if (usage == 0x34) return shift ? '"' : '\'';
    if (usage == 0x35) return shift ? '~' : '`';
    if (usage == 0x36) return shift ? '<' : ',';
    if (usage == 0x37) return shift ? '>' : '.';
    if (usage == 0x38) return shift ? '?' : '/';
    if (usage == 0x28) return '\n';
    if (usage == 0x2A) return '\b';
    if (usage == 0x2B) return '\t';
    return 0;
}

void usb_keyboard_process_report(const uint8_t* report, uint8_t length) {
    if (!report || length < 8) return;

    if (!report_logged) {
        serial_write("USB keyboard HID boot reports received.\n");
        report_logged = 1;
    }

    uint8_t modifiers = report[0];
    uint8_t shift = (modifiers & 0x22) != 0;

    for (uint8_t key_index = 0; key_index < 6; key_index++) {
        uint8_t usage = report[2 + key_index];
        if (usage == 0 || usage == 1) continue;

        uint8_t was_down = 0;
        for (uint8_t previous_index = 0; previous_index < 6; previous_index++) {
            if (previous_keys[previous_index] == usage) {
                was_down = 1;
                break;
            }
        }
        if (was_down) continue;

        serial_write("USB key down usage=0x");
        serial_write_hex8(usage);
        char ascii = usb_keyboard_usage_to_ascii(usage, shift);
        if (ascii) {
            serial_write(" char='");
            if (ascii == '\n') serial_write("\\n");
            else if (ascii == '\t') serial_write("\\t");
            else if (ascii == '\b') serial_write("\\b");
            else serial_write_char(ascii);
            serial_write("'");
        }
        serial_write("\n");
    }

    if (modifiers != previous_modifiers) {
        serial_write("USB keyboard modifiers=0x");
        serial_write_hex8(modifiers);
        serial_write("\n");
        previous_modifiers = modifiers;
    }

    for (uint8_t key_index = 0; key_index < 6; key_index++) {
        previous_keys[key_index] = report[2 + key_index];
    }
}