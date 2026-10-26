#ifndef KEYBOARD_PS2_H
#define KEYBOARD_PS2_H

#include <stdbool.h>
#include <stdint.h>

void keyboard_init(void);
void keyboard_handler(void);
void keyboard_process_scancode(uint8_t base_scancode, bool is_released);
uint8_t keyboard_get_last_scancode(void);
void keyboard_clear_last_scancode(void);
bool keyboard_is_key_pressed(int key_code);

#endif