// kernel/serial.c
#include <kernel/serial.h>

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
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
    while (*str) {
        serial_write_char(*str++);
    }
}