// src/lib/font/font8x16_basic.c
#include <kernel/drivers/video/font/font8x16_basic.h>
#include <kernel/drivers/video/font/font8x8_basic.h>
#include <stdint.h>

static inline uint8_t row8(uint8_t ch, int r8) {
    const uint8_t* g = font8x8_basic[ch];
    return (r8 < 0 || r8 > 7) ? 0 : g[r8];
}

// Overlay bitmasks (bit 7 is the leftmost pixel)
#define BREVE_ROW0 0x18  //   **
#define BREVE_ROW1 0x24  //  *  *
#define UMLAUT     0x24  //  *  *  (colon)
#define CEDIL_ROW  0x18  //   **  (hook)

uint8_t font8x16_basic_row(uint8_t ch, int row) {
    if (row < 0) row = 0;
    if (row > 15) row = 15;

    // Basic 2x scaled letters
    uint8_t base = row8(ch, row >> 1);

    // --- ğ (0xF0): The original g is not altered, breve letters are added consecutively ---
    if (ch == 0xF0) {
        uint8_t line = row8('g', row >> 1);
        if (row == 0) line |= BREVE_ROW0;
        if (row == 1) line |= BREVE_ROW1;
        return line;
    }

    // --- Ğ (0xD0): The original G is not altered, breve letters are added consecutively ---
    if (ch == 0xD0) {
        uint8_t line = row8('G', row >> 1);
        if (row == 0) line |= BREVE_ROW0;
        if (row == 1) line |= BREVE_ROW1;
        return line;
    }

    // --- ü (0xFC): The original u is not altered, dots are added consecutively ---
    if (ch == 0xFC) {
        uint8_t line = row8('u', row >> 1);
        if (row == 0 || row == 1) line |= UMLAUT;
        return line;
    }

    // --- Ü (0xDC): The original U is not altered, dots are added consecutively ---
    // (Since the upper-middle part of the letter U is empty, the 0x24 dots do not overlap)
    if (ch == 0xDC) {
        uint8_t line = row8('U', row >> 1);
        if (row == 0 || row == 1) line |= UMLAUT;
        return line;
    }

    // --- ç (0xE7): c + cedilla ---
    if (ch == 0xE7) {
        uint8_t line = row8('c', row >> 1);
        if (row == 14) line |= CEDIL_ROW;
        if (row == 15) line |= 0x10;
        return line;
    }

    // --- Ç (0xC7): C + cedilla ---
    if (ch == 0xC7) {
        uint8_t line = row8('C', row >> 1);
        if (row == 14) line |= CEDIL_ROW;
        if (row == 15) line |= 0x10;
        return line;
    }

    // --- ş (0xFE): s + cedilla ---
    if (ch == 0xFE) {
        uint8_t line = row8('s', row >> 1);
        if (row == 14) line |= CEDIL_ROW;
        if (row == 15) line |= 0x10;
        return line;
    }

    // --- Ş (0xDE): S + cedilla ---
    if (ch == 0xDE) {
        uint8_t line = row8('S', row >> 1);
        if (row == 14) line |= CEDIL_ROW;
        if (row == 15) line |= 0x10;
        return line;
    }

    // --- İ (0xDD): I-sized glyph with a dot added above ---
    if (ch == 0xDD) {
        uint8_t line = row8('I', row >> 1);
        if (row == 0 || row == 1) line |= 0x18; // Add a centered dot above
        return line;
    }

    // --- ı (0xFD): Lowercase dotless ı (i without the upper dot) ---
    if (ch == 0xFD) {
        uint8_t line = row8('i', row >> 1);
        if (row <= 2) line = 0; // Clear the upper dot
        return line;
    }

    // --- é (0xE9): e + acute accent (´) ---
    if (ch == 0xE9) {
        uint8_t line = row8('e', row >> 1);
        if (row == 0) line |= 0x30;
        if (row == 1) line |= 0x18;
        return line;
    }

    return base;
}