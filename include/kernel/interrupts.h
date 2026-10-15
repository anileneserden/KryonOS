#ifndef KERNEL_INTERRUPTS_H
#define KERNEL_INTERRUPTS_H

#include <stdint.h>

void interrupts_init(void);
uint8_t interrupts_take_input_pending(void);
void interrupts_wait_for_input(void);

#endif