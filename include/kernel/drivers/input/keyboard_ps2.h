#ifndef KEYBOARD_PS2_H
#define KEYBOARD_PS2_H

#include <stdbool.h>
#include <stdint.h>

void keyboard_init(void);
void keyboard_handler(void);
uint8_t keyboard_get_last_scancode(void);
void keyboard_clear_last_scancode(void);
bool keyboard_is_key_pressed(int key_code);

#endif