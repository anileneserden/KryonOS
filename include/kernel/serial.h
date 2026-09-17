#pragma once

#include <stdint.h>

#define SERIAL_COM1_PORT 0x3F8

void serial_init(void);
void serial_write_char(char a);
void serial_write(const char* str);
void serial_write_dec(uint32_t n);
void serial_write_num(uint32_t n);