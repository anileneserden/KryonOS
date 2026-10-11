// kernel/serial.c
#include <kernel/serial.h>
#include <arch/x86/io.h>
#include <ui/serial_screen.h>
#include <stdbool.h>

static bool serial_screen_mirror_active = false;

void serial_screen_mirror_enable(bool enable) {
    serial_screen_mirror_active = enable;
}

void serial_init(void) {
    outb(SERIAL_COM1_PORT + 1, 0x00);    // Disable interrupts
    outb(SERIAL_COM1_PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(SERIAL_COM1_PORT + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
    outb(SERIAL_COM1_PORT + 1, 0x00);    // (hi byte)
    outb(SERIAL_COM1_PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(SERIAL_COM1_PORT + 2, 0xC7);    // Enable FIFO, clear with 14-byte threshold
    outb(SERIAL_COM1_PORT + 4, 0x0B);    // Enable IRQs, RTS/DSR set
}

static int serial_is_transmit_empty(void) {
    return inb(SERIAL_COM1_PORT + 5) & 0x20;
}

void serial_write_char(char a) {
    while (serial_is_transmit_empty() == 0);
    outb(SERIAL_COM1_PORT, (uint8_t)a);
}

void serial_write(const char* str) {
    const char* tmp = str;
    while (*tmp) {
        serial_write_char(*tmp++);
    }

    // Eğer debug ekranı aktifse, yazılan metni ekrana da aynen yansıt
    if (serial_screen_mirror_active) {
        serial_screen_write(str);
    }
}

void serial_write_dec(uint32_t n) {
    if (n == 0) {
        serial_write_char('0');
        return;
    }
    
    char buf[32];
    int i = 0;
    
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    
    while (i > 0) {
        serial_write_char(buf[--i]);
    }
}

void serial_write_num(uint32_t n) {
    serial_write_dec(n);
}