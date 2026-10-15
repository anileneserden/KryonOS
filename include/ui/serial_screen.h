#ifndef UI_SERIAL_SCREEN_H
#define UI_SERIAL_SCREEN_H

#include <stdint.h>
#include <stdbool.h>

void serial_screen_init(void);
void serial_screen_write(const char* str);
void serial_screen_handle_input(uint8_t scancode);

#endif