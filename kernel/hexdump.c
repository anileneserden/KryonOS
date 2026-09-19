#include <kernel/hexdump.h>
#include <kernel/serial.h>

void kernel_hexdump(const void* data, size_t size) {
    const uint8_t* ptr = (const uint8_t*)data;
    
    serial_write("\n--- HEXDUMP (Boyut: ");
    serial_write_num(size);
    serial_write(" bayt) ---\n");

    for (size_t i = 0; i < size; i += 16) {
        // Hex kısımlarını yazdır
        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                uint8_t b = ptr[i + j];
                char hex1 = (b >> 4) < 10 ? '0' + (b >> 4) : 'A' + ((b >> 4) - 10);
                char hex2 = (b & 0x0F) < 10 ? '0' + (b & 0x0F) : 'A' + ((b & 0x0F) - 10);
                serial_write_char(hex1);
                serial_write_char(hex2);
                serial_write(" ");
            } else {
                serial_write("   ");
            }
        }

        serial_write(" | ");

        // ASCII karşılıklarını yazdır
        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                uint8_t b = ptr[i + j];
                if (b >= 32 && b <= 126) {
                    serial_write_char(b);
                } else {
                    serial_write_char('.');
                }
            }
        }
        serial_write("\n");
    }
    serial_write("-------------------------------------\n");
}