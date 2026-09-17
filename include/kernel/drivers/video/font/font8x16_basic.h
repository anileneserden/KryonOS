#pragma once
#include <stdint.h>

// 8x16 font: returns an 8-bit bitmap for rows 0..15.
// Bit order: left to right (bit 7 is the leftmost pixel), compatible with fb_console.
uint8_t font8x16_basic_row(uint8_t ch, int row);
