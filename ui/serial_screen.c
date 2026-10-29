#include <ui/serial_screen.h>
#include <kernel/drivers/video/fb.h>
#include <kernel/drivers/video/gfx.h>
#include <stddef.h>
#include <stdbool.h>

#define MAX_LOG_LINES 100
#define MAX_LINE_LEN 128
#define VISIBLE_LINES 30

static char log_buffer[MAX_LOG_LINES][MAX_LINE_LEN];
static int current_line_count = 0;
static int current_col = 0;
static int scroll_offset = 0;

static void serial_screen_redraw(void) {
    fb_clear(0xFF000000);
    int y_offset = 10;
    
    if (scroll_offset < 0) scroll_offset = 0;
    
    int max_scroll = current_line_count - VISIBLE_LINES;
    if (max_scroll < 0) max_scroll = 0;
    if (scroll_offset > max_scroll) scroll_offset = max_scroll;

    for (int i = 0; i < VISIBLE_LINES; i++) {
        int line_idx = scroll_offset + i;
        if (line_idx <= current_line_count && line_idx < MAX_LOG_LINES) {
            gfx_draw_text(10, y_offset, 0xFF00FF66, log_buffer[line_idx]);
        }
        y_offset += 18;
    }
    
    fb_swap();
}

// Basitçe basılan scancode değerini ekrana yazdıran yardımcı fonksiyon
static void log_scancode_to_screen(uint8_t code) {
    char buf[32];
    char hex[] = "0123456789ABCDEF";
    
    buf[0] = 'K'; buf[1] = 'e'; buf[2] = 'y'; buf[3] = ':'; buf[4] = ' ';
    buf[5] = '0'; buf[6] = 'x';
    buf[7] = hex[(code >> 4) & 0xF];
    buf[8] = hex[code & 0xF];
    buf[9] = '\n';
    buf[10] = '\0';
    
    serial_screen_write(buf);
}

void serial_screen_init(void) {
    fb_clear(0xFF000000);
    current_line_count = 0;
    current_col = 0;
    scroll_offset = 0;
    
    serial_screen_write("=== LENOROS KERNEL DEBUG SCREEN ===\n");
    serial_screen_write("Use Up/Down or PageUp/PageDown to scroll logs.\n");
}

void serial_screen_write(const char* str) {
    if (!str) return;

    while (*str) {
        char c = *str++;

        if (c == '\n') {
            log_buffer[current_line_count][current_col] = '\0';
            current_line_count++;
            current_col = 0;
            
            if (current_line_count >= MAX_LOG_LINES) {
                for (int i = 0; i < MAX_LOG_LINES - 1; i++) {
                    for (int j = 0; j < MAX_LINE_LEN; j++) {
                        log_buffer[i][j] = log_buffer[i + 1][j];
                    }
                }
                current_line_count = MAX_LOG_LINES - 1;
            }
        } else {
            if (current_col < MAX_LINE_LEN - 1) {
                log_buffer[current_line_count][current_col++] = c;
                log_buffer[current_line_count][current_col] = '\0';
            }
        }
    }

    if (current_line_count > VISIBLE_LINES) {
        scroll_offset = current_line_count - VISIBLE_LINES;
    } else {
        scroll_offset = 0;
    }

    serial_screen_redraw();
}

void serial_screen_handle_input(uint8_t scancode) {
    // Basılan scancode değerini ekranda görmek istiyorsan loglamaya devam edebiliriz:
    //log_scancode_to_screen(scancode);

    // Tuş bırakılma (release) kodlarını (bit 7 aktif, örn: 0x81, 0x9F) atlıyoruz
    if (scancode & 0x80) {
        return;
    }

    // W ve S tuşları ile kaydırma (W = Yukarı, S = Aşağı)
    switch (scancode) {
        case 0x11: // W tuşu (Yukarı kaydır)
            if (scroll_offset > 0) {
                scroll_offset -= 1; // İstersen hızı ayarlamak için artırıp azaltabilirsin
                if (scroll_offset < 0) scroll_offset = 0;
                serial_screen_redraw();
            }
            break;
            
        case 0x1F: // S tuşu (Aşağı kaydır)
        {
            int max_scroll = current_line_count - VISIBLE_LINES;
            if (max_scroll < 0) max_scroll = 0;
            if (scroll_offset < max_scroll) {
                scroll_offset += 1;
                if (scroll_offset > max_scroll) scroll_offset = max_scroll;
                serial_screen_redraw();
            }
            break;
        }
        default:
            break;
    }
}