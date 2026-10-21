// kernel/serial.c
#include <kernel/serial.h>
#include <arch/x86/io.h>
#include <ui/serial_screen.h>
#include <kernel/drivers/rtc.h>
#include <stdbool.h>

static bool serial_screen_mirror_active = false;
static bool new_line_started = true;

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

// Yardımcı fonksiyon: Sayıları iki haneli stringe çevirir (Örn: 9 -> "09")
static void serial_write_two_digits(uint8_t n) {
    char buf[3];
    buf[0] = '0' + (n / 10);
    buf[1] = '0' + (n % 10);
    buf[2] = '\0';
    
    const char* tmp = buf;
    while (*tmp) {
        while (serial_is_transmit_empty() == 0);
        outb(SERIAL_COM1_PORT, (uint8_t)*tmp++);
    }
}

// Ham string yazma (timestamp tetiklemeden)
static void serial_write_raw(const char* str) {
    while (*str) {
        while (serial_is_transmit_empty() == 0);
        outb(SERIAL_COM1_PORT, (uint8_t)*str++);
    }
}

// Zaman damgası ekleyen fonksiyon
static void serial_print_timestamp(void) {
    rtc_time_t t;
    rtc_get_time(&t);

    serial_write_raw("[");
    serial_write_two_digits(t.day);
    serial_write_raw(".");
    serial_write_two_digits(t.month);
    serial_write_raw(".");
    
    // Yıl yazdırma (4 haneli)
    uint32_t yr = t.year;
    char y_str[5];
    y_str[4] = '\0';
    for (int i = 3; i >= 0; i--) {
        y_str[i] = '0' + (yr % 10);
        yr /= 10;
    }
    serial_write_raw(y_str);

    serial_write_raw("-");
    serial_write_two_digits(t.hour);
    serial_write_raw(".");
    serial_write_two_digits(t.minute);
    serial_write_raw(".");
    serial_write_two_digits(t.second);
    serial_write_raw("] ");
}

void serial_write(const char* str) {
    const char* tmp = str;
    while (*tmp) {
        if (new_line_started) {
            serial_print_timestamp();
            new_line_started = false;
        }

        serial_write_char(*tmp);

        if (*tmp == '\n') {
            new_line_started = true;
        }
        tmp++;
    }

    // Eğer debug ekranı aktifse, yazılan metni ekrana da aynen yansıt
    if (serial_screen_mirror_active) {
        serial_screen_write(str);
    }
}

void serial_write_dec(uint32_t n) {
    if (n == 0) {
        serial_write("0");
        return;
    }
    
    char buf[32];
    int i = 0;
    
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    buf[i] = '\0';
    
    // Diziyi ters çevir
    for (int j = 0; j < i / 2; j++) {
        char temp = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = temp;
    }
    
    serial_write(buf);
}

void serial_write_num(uint32_t n) {
    serial_write_dec(n);
}

void serial_write_hex(uint32_t n) {
    char hex_chars[] = "0123456789ABCDEF";
    char buf[9];
    buf[8] = '\0';
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex_chars[n & 0xF];
        n >>= 4;
    }
    serial_write(buf);
}

void serial_write_hex8(uint8_t n) {
    char hex_chars[] = "0123456789ABCDEF";
    char buf[3];
    buf[0] = hex_chars[(n >> 4) & 0xF];
    buf[1] = hex_chars[n & 0xF];
    buf[2] = '\0';
    
    serial_write(buf);
}

void serial_write_hex16(uint16_t n) {
    char hex_chars[] = "0123456789ABCDEF";
    char buf[5];
    buf[0] = hex_chars[(n >> 12) & 0xF];
    buf[1] = hex_chars[(n >> 8) & 0xF];
    buf[2] = hex_chars[(n >> 4) & 0xF];
    buf[3] = hex_chars[n & 0xF];
    buf[4] = '\0';
    
    serial_write(buf);
}