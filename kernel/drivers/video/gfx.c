/* kernel/drivers/video/gfx.c */

#include <kernel/drivers/video/gfx.h>
#include <kernel/drivers/video/fb.h>
#include <kernel/drivers/video/font/font8x16_basic.h>

void gfx_draw_pixel(int x, int y, uint32_t color) {
    fb_putpixel(x, y, color);
}

void gfx_fill_rect(int x, int y, int width, int height, uint32_t color) {
    for (int cy = 0; cy < height; cy++) {
        for (int cx = 0; cx < width; cx++) {
            gfx_draw_pixel(x + cx, y + cy, color);
        }
    }
}

void gfx_draw_rect(int x, int y, int width, int height, uint32_t color) {
    if (width <= 0 || height <= 0) return;

    // Üst ve Alt Kenar çizgileri
    for (int cx = 0; cx < width; cx++) {
        gfx_draw_pixel(x + cx, y, color);               // Üst kenar
        gfx_draw_pixel(x + cx, y + height - 1, color);  // Alt kenar
    }

    // Sol ve Sağ Kenar çizgileri (köşeler dahil)
    for (int cy = 0; cy < height; cy++) {
        gfx_draw_pixel(x, y + cy, color);               // Sol kenar
        gfx_draw_pixel(x + width - 1, y + cy, color);   // Sağ kenar
    }
}

void gfx_fill_rounded_rect(int x, int y, int width, int height, int radius, uint32_t color) {
    for (int cy = 0; cy < height; cy++) {
        for (int cx = 0; cx < width; cx++) {
            int draw_pixel = 1;

            // Sol-Üst Köşe
            if (cx < radius && cy < radius) {
                int dx = radius - cx;
                int dy = radius - cy;
                if ((dx * dx + dy * dy) > (radius * radius)) {
                    draw_pixel = 0;
                }
            }
            // Sağ-Üst Köşe
            else if (cx >= width - radius && cy < radius) {
                int dx = cx - (width - radius - 1);
                int dy = radius - cy;
                if ((dx * dx + dy * dy) > (radius * radius)) {
                    draw_pixel = 0;
                }
            }
            // Sol-Alt Köşe
            else if (cx < radius && cy >= height - radius) {
                int dx = radius - cx;
                int dy = cy - (height - radius - 1);
                if ((dx * dx + dy * dy) > (radius * radius)) {
                    draw_pixel = 0;
                }
            }
            // Sağ-Alt Köşe
            else if (cx >= width - radius && cy >= height - radius) {
                int dx = cx - (width - radius - 1);
                int dy = cy - (height - radius - 1);
                if ((dx * dx + dy * dy) > (radius * radius)) {
                    draw_pixel = 0;
                }
            }

            if (draw_pixel) {
                gfx_draw_pixel(x + cx, y + cy, color);
            }
        }
    }
}

void gfx_draw_text(int x, int y, uint32_t color, const char* s) {
    if (!s) return;

    while (*s) {
        uint8_t c = (uint8_t)*s++;

        for (int row = 0; row < 16; row++) {
            uint8_t line = font8x16_basic_row(c, row);

            for (int col = 0; col < 8; col++) {
                if (line & (1u << (7 - col))) {
                    gfx_draw_pixel(x + col, y + row, color);
                }
            }
        }

        x += 8;
    }
}

/* Basit UTF-8 kod çözücü yardımcı fonksiyonu */
static uint32_t utf8_next(const char** s_ptr) {
    const uint8_t* s = (const uint8_t*)*s_ptr;
    if (!s || !*s) return 0;

    uint32_t cp = 0;
    int bytes = 0;

    if (*s < 0x80) {
        cp = *s;
        bytes = 1;
    } else if ((*s & 0xE0) == 0xC0) {
        cp = *s & 0x1F;
        bytes = 2;
    } else if ((*s & 0xF0) == 0xE0) {
        cp = *s & 0x0F;
        bytes = 3;
    } else if ((*s & 0xF8) == 0xF0) {
        cp = *s & 0x07;
        bytes = 4;
    } else {
        (*s_ptr)++;
        return '?';
    }

    // Devam baytlarını işle
    for (int i = 1; i < bytes; i++) {
        if ((s[i] & 0xC0) != 0x80) break;
        cp = (cp << 6) | (s[i] & 0x3F);
    }

    *s_ptr += bytes;
    return cp;
}

/* unicode -> KVX 0..255 charset */
static uint8_t unicode_to_kvx_byte(uint32_t cp) {
    if (cp < 0x80) return (uint8_t)cp;

    switch (cp) {
        case 0x00FC: return 0xFC; // ü
        case 0x00DC: return 0xDC; // Ü
        case 0x00F6: return 0xF6; // ö
        case 0x00D6: return 0xD6; // Ö
        case 0x00E7: return 0xE7; // ç
        case 0x00C7: return 0xC7; // Ç
        case 0x011F: return 0xF0; // ğ
        case 0x011E: return 0xD0; // Ğ
        case 0x015F: return 0xFE; // ş
        case 0x015E: return 0xDE; // Ş
        case 0x0131: return 0xFD; // ı
        case 0x0130: return 0xDD; // İ
        case 0x00D7: return 0xF7; // ×
        case 0x00F7: return 0xF8; // ÷
        default:     return '?';
    }
}

void gfx_draw_text_utf8(int x, int y, uint32_t color, const char* s) {
    if (!s) return;

    char out[256];
    int oi = 0;

    while (*s) {
        uint32_t cp = utf8_next(&s);
        if (!cp) break;

        uint8_t ch = unicode_to_kvx_byte(cp);

        if (ch == '\n' || ch == '\r') {
            out[oi] = '\0';
            if (oi) gfx_draw_text(x, y, color, out);
            oi = 0;
            y += 16;
            continue;
        }

        out[oi++] = (char)ch;

        if (oi >= (int)sizeof(out) - 1) {
            out[oi] = '\0';
            gfx_draw_text(x, y, color, out);
            oi = 0;
        }
    }

    out[oi] = '\0';
    if (oi) gfx_draw_text(x, y, color, out);
}