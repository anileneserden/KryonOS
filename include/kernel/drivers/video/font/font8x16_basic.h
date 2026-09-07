#pragma once
#include <stdint.h>

// 8x16 font: row 0..15 için 8-bit bitmap döndürür.
// Bit yönü: soldan sağa (bit7 soldaki piksel), fb_console ile uyumlu.
uint8_t font8x16_basic_row(uint8_t ch, int row);
