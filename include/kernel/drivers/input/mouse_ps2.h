#ifndef KERNEL_DRIVERS_INPUT_MOUSE_PS2_H
#define KERNEL_DRIVERS_INPUT_MOUSE_PS2_H

#include <stdint.h>

extern int32_t mouse_x;
extern int32_t mouse_y;
extern uint8_t mouse_buttons;

void mouse_init(void);
void mouse_handler(void);
void mouse_wait(uint8_t type);
uint8_t mouse_read(void);
void mouse_write(uint8_t data);

#endif