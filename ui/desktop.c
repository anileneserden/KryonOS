#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <kernel/string.h>
#include <kernel/mem/heap.h>
#include <ui/desktop_icons.h>
#include <ui/desktop.h>
#include <ui/wm.h>
#include <ui/cursor.h>
#include <ui/grid.h>
#include <kernel/fs/vfs.h>
#include <kernel/drivers/video/fb.h>
#include <kernel/drivers/video/gfx.h>
#include <kernel/drivers/input/keyboard_ps2.h>
#include <arch/x86/io.h>
#include <kernel/power.h>
#include <kernel/drivers/rtc.h>

int snprintf(char *str, size_t size, const char *format, ...);

static int timezone_offset = 3;
uint32_t desktop_bg_color = 0xFF1E1E1E;

#define MENU_W 400
#define MENU_H 500

extern uint8_t mouse_buttons;

typedef struct {
    int x, y, w, h;
    bool active;
} damage_rect_t;

static damage_rect_t screen_damage = {0, 0, 0, 0, false};

static bool start_menu_open = false;
static bool prev_mouse_left = false;

static bool right_menu_open = false;
static bool prev_mouse_right = false;
static int right_menu_x = 0;
static int right_menu_y = 0;

#define RIGHT_MENU_W 160
#define RIGHT_MENU_H 100

static bool file_exists(const char* path) {
    return vfs_file_exists(path);
}

static void get_unique_filename(const char *base_dir, const char *base_name, const char *ext, char *out_path, size_t max_len) {
    int counter = 0;
    while (1) {
        if (counter == 0) {
            snprintf(out_path, max_len, "%s/%s%s", base_dir, base_name, ext);
        } else {
            snprintf(out_path, max_len, "%s/%s (%d)%s", base_dir, base_name, counter, ext);
        }

        if (!file_exists(out_path)) {
            break;
        }
        counter++;
    }
}

void damage_clear(void) {
    screen_damage.active = false;
    screen_damage.x = 0;
    screen_damage.y = 0;
    screen_damage.w = 0;
    screen_damage.h = 0;
}

void damage_union_rect(int x, int y, int w, int h) {
    int sw = fb_get_width();
    int sh = fb_get_height();

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > sw) w = sw - x;
    if (y + h > sh) h = sh - y;
    if (w <= 0 || h <= 0) return;

    if (!screen_damage.active) {
        screen_damage.x = x;
        screen_damage.y = y;
        screen_damage.w = w;
        screen_damage.h = h;
        screen_damage.active = true;
    } else {
        int min_x = (screen_damage.x < x) ? screen_damage.x : x;
        int min_y = (screen_damage.y < y) ? screen_damage.y : y;
        int max_x = ((screen_damage.x + screen_damage.w) > (x + w)) ? (screen_damage.x + screen_damage.w) : (x + w);
        int max_y = ((screen_damage.y + screen_damage.h) > (y + h)) ? (screen_damage.y + screen_damage.h) : (y + h);

        screen_damage.x = min_x;
        screen_damage.y = min_y;
        screen_damage.w = max_x - min_x;
        screen_damage.h = max_y - min_y;
    }
}

void desktop_redraw(void) {
    if (!screen_damage.active) return;

    cursor_prepare_redraw();

    gfx_fill_rect(screen_damage.x, screen_damage.y, screen_damage.w, screen_damage.h, desktop_bg_color);

    int screen_w = fb_get_width();
    int screen_h = fb_get_height();
    int cell_w = grid_get_cell_width();
    int cell_h = grid_get_cell_height();
    uint32_t grid_line_color = 0xFF2A2A2A;

    for (int x = 0; x <= screen_w; x += cell_w) {
        if (x >= screen_damage.x && x <= screen_damage.x + screen_damage.w) {
            gfx_fill_rect(x, screen_damage.y, 1, screen_damage.h, grid_line_color);
        }
    }

    for (int y = 0; y <= screen_h; y += cell_h) {
        if (y >= screen_damage.y && y <= screen_damage.y + screen_damage.h) { 
            gfx_fill_rect(screen_damage.x, y, screen_damage.w, 1, grid_line_color);
        }
    }

    int cur_x = cursor_get_x();
    int cur_y = cursor_get_y();
    desktop_icons_draw(cur_x, cur_y, false);

    wm_draw_all();

    // --- SOL ÜST KÖŞE FARE KOORDİNAT PANELİ ---
    char coord_str[32];
    snprintf(coord_str, sizeof(coord_str), "mouse-x: %d mouse-y: %d", cur_x, cur_y);
    
    gfx_fill_rect(8, 8, 220, 24, 0xEE181818);
    gfx_draw_rect(8, 8, 220, 24, 0xFF444444);
    gfx_draw_text(14, 12, 0xFFFFFFFF, coord_str);
    // ------------------------------------------

    int taskbar_h = 36;
    int taskbar_y = screen_h - taskbar_h;
    if (screen_damage.y + screen_damage.h >= taskbar_y) {
        gfx_fill_rect(screen_damage.x, taskbar_y > screen_damage.y ? taskbar_y : screen_damage.y, 
                      screen_damage.w, taskbar_h, 0xFF181818);
        gfx_fill_rect(screen_damage.x, taskbar_y, screen_damage.w, 1, 0xFF333333);

        int btn_x = 4;
        int btn_y = taskbar_y + 4;
        int btn_w = 70;
        int btn_h = 28;

        uint32_t btn_bg = start_menu_open ? 0xFF2A2A2A : 0xFF3A3A3A;
        gfx_fill_rect(btn_x, btn_y, btn_w, btn_h, btn_bg);
        
        gfx_fill_rect(btn_x, btn_y, btn_w, 1, 0xFF555555);
        gfx_fill_rect(btn_x, btn_y, 1, btn_h, 0xFF555555);
        gfx_fill_rect(btn_x + btn_w - 1, btn_y, 1, btn_h, 0xFF111111);
        gfx_fill_rect(btn_x, btn_y + btn_h - 1, btn_w, 1, 0xFF111111);

        rtc_time_t t;
        rtc_get_time(&t);

        int adjusted_hour = (int)t.hour + timezone_offset;
        while (adjusted_hour < 0) adjusted_hour += 24;
        adjusted_hour %= 24;

        int clock_w = 70;
        int clock_h = 24;
        int clock_x = screen_w - clock_w - 8;
        int clock_y = taskbar_y + 6;

        gfx_fill_rect(clock_x, clock_y, clock_w, clock_h, 0xFF2A2A2A);
        gfx_fill_rect(clock_x, clock_y, clock_w, 1, 0xFF111111);
        gfx_fill_rect(clock_x, clock_y, 1, clock_h, 0xFF111111);
        gfx_fill_rect(clock_x + clock_w - 1, clock_y, 1, clock_h, 0xFF555555);
        gfx_fill_rect(clock_x, clock_y + clock_h - 1, clock_w, 1, 0xFF555555);

        char time_str[6];
        time_str[0] = '0' + (adjusted_hour / 10);
        time_str[1] = '0' + (adjusted_hour % 10);
        time_str[2] = ':';
        time_str[3] = '0' + (t.minute / 10);
        time_str[4] = '0' + (t.minute % 10);
        time_str[5] = '\0';

        gfx_draw_text(clock_x + 10, clock_y + 6, 0xFFFFFFFF, time_str);
    }

    if (start_menu_open) {
        int menu_x = 4;
        int menu_w = MENU_W;
        int menu_h = MENU_H;
        int menu_y = taskbar_y - menu_h;

        gfx_fill_rect(menu_x, menu_y, menu_w, menu_h, 0xFF222222);
        gfx_fill_rect(menu_x, menu_y, 24, menu_h, 0xFF333333);

        int shut_w = 100;
        int shut_h = 32;
        int shut_x = menu_x + menu_w - shut_w - 12;
        int shut_y = menu_y + menu_h - shut_h - 12;

        gfx_fill_rect(shut_x, shut_y, shut_w, shut_h, 0xFF4A2222);
        gfx_fill_rect(shut_x, shut_y, shut_w, 1, 0xFF663333);
        gfx_fill_rect(shut_x, shut_y, 1, shut_h, 0xFF663333);
        gfx_fill_rect(shut_x + shut_w - 1, shut_y, 1, shut_h, 0xFF221111);
        gfx_fill_rect(shut_x, shut_y + shut_h - 1, shut_w, 1, 0xFF221111);

        gfx_draw_text(shut_x + 22, shut_y + 8, 0xFFFFFFFF, "Shut Down");

        gfx_fill_rect(menu_x, menu_y, menu_w, 1, 0xFF666666);
        gfx_fill_rect(menu_x, menu_y, 1, menu_h, 0xFF666666);
        gfx_fill_rect(menu_x + menu_w - 1, menu_y, 1, menu_h, 0xFF111111);
        gfx_fill_rect(menu_x, menu_y + menu_h - 1, menu_w, 1, 0xFF111111);
    }

    if (right_menu_open) {
        int r_w = RIGHT_MENU_W;
        int r_h = RIGHT_MENU_H;
        int r_x = right_menu_x;
        int r_y = right_menu_y;

        if (r_x + r_w > screen_w) r_x = screen_w - r_w;
        if (r_y + r_h > screen_h) r_y = screen_h - r_h;

        gfx_fill_rect(r_x, r_y, r_w, r_h, 0xFF222222);

        bool hover_yenile = (cur_x >= r_x + 4 && cur_x <= r_x + r_w - 4 &&
                             cur_y >= r_y + 6 && cur_y <= r_y + 30);
        uint32_t bg_yenile = hover_yenile ? 0xFF3A3A3A : 0xFF2A2A2A;
        
        gfx_fill_rect(r_x + 4, r_y + 6, r_w - 8, 24, bg_yenile);
        gfx_draw_text(r_x + 12, r_y + 10, 0xFFFFFFFF, "Refresh");

        bool hover_terminal = (cur_x >= r_x + 4 && cur_x <= r_x + r_w - 4 &&
                               cur_y >= r_y + 34 && cur_y <= r_y + 58);
        uint32_t bg_terminal = hover_terminal ? 0xFF3A3A3A : 0xFF2A2A2A;

        gfx_fill_rect(r_x + 4, r_y + 34, r_w - 8, 24, bg_terminal);
        gfx_draw_text(r_x + 12, r_y + 38, 0xFFFFFFFF, "Terminal");

        bool hover_newfile = (cur_x >= r_x + 4 && cur_x <= r_x + r_w - 4 &&
                              cur_y >= r_y + 62 && cur_y <= r_y + 86);
        uint32_t bg_newfile = hover_newfile ? 0xFF3A3A3A : 0xFF2A2A2A;
        gfx_fill_rect(r_x + 4, r_y + 62, r_w - 8, 24, bg_newfile);
        gfx_draw_text(r_x + 12, r_y + 66, 0xFFFFFFFF, "New File");

        gfx_fill_rect(r_x, r_y, r_w, 1, 0xFF666666);
        gfx_fill_rect(r_x, r_y, 1, r_h, 0xFF666666);
        gfx_fill_rect(r_x + r_w - 1, r_y, 1, r_h, 0xFF111111);
        gfx_fill_rect(r_x, r_y + r_h - 1, r_w, 1, 0xFF111111);
    }

    cursor_show_internal(false);
    fb_blit_region(screen_damage.x, screen_damage.y, screen_damage.w, screen_damage.h);
    
    damage_clear();
}

void desktop_init(void) {
    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    if (width == 0 || height == 0) return;

    uint32_t cfg_size = 0;
    char* cfg_content = (char*)vfs_read_file("C:/Kryon/System32/settings.cfg", &cfg_size);
    if (cfg_content && cfg_size > 0) {
        for (uint32_t i = 0; i < cfg_size - 6; i++) {
            if (strncmp(&cfg_content[i], "offset=", 7) == 0) {
                int val = 0;
                int sign = 1;
                uint32_t idx = i + 7;
                
                if (cfg_content[idx] == '-') {
                    sign = -1;
                    idx++;
                } else if (cfg_content[idx] == '+') {
                    idx++;
                }

                while (idx < cfg_size && cfg_content[idx] >= '0' && cfg_content[idx] <= '9') {
                    val = val * 10 + (cfg_content[idx] - '0');
                    idx++;
                }
                timezone_offset = val * sign;
                break;
            }
        }

        // 2. Desktop Background Color Parse Etme
        for (uint32_t i = 0; i < cfg_size - 9; i++) {
            if (strncmp(&cfg_content[i], "backcolor=", 10) == 0) {
                int idx = i + 10;
                uint32_t parsed_color = 0;

                if (cfg_content[idx] == '0' && (cfg_content[idx+1] == 'x' || cfg_content[idx+1] == 'X')) {
                    idx += 2;
                }

                while (idx < cfg_size) {
                    char c = cfg_content[idx];
                    uint8_t nibble = 0;
                    if (c >= '0' && c <= '9') nibble = c - '0';
                    else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
                    else break;

                    parsed_color = (parsed_color << 4) | nibble;
                    idx++;
                }

                if ((parsed_color & 0xFF000000) == 0) {
                    parsed_color |= 0xFF000000;
                }

                desktop_bg_color = parsed_color;
                break;
            }
        }

        kfree(cfg_content);
    }

    // 1. ÖNCE imleci başlat (PNG yüklenir, boyutlar oturur ve cursor_visible = false olur)
    cursor_init();
    cursor_sync_position();

    // 2. Grid, ikonlar ve pencere yöneticisini başlat
    grid_init(width, height, 100, 100);
    desktop_icons_init();
    wm_init();

    // 3. Ekranı ilk kez çizime hazırla ve tetikle
    damage_union_rect(0, 0, width, height);
    desktop_redraw();
}

void desktop_process_input(void) {
    uint8_t scancode = keyboard_get_last_scancode();
    if (scancode == 0x5B) {
        start_menu_open = !start_menu_open;
        right_menu_open = false;

        int screen_w = fb_get_width();
        int screen_h = fb_get_height();
        damage_union_rect(0, 0, screen_w, screen_h);
        desktop_redraw();

        keyboard_clear_last_scancode();
    }

    int old_x = cursor_get_old_x();
    int old_y = cursor_get_old_y();

    wm_process_input();

    int new_x = cursor_get_x();
    int new_y = cursor_get_y();
    int cursor_w = cursor_get_width();   
    int cursor_h = cursor_get_height();  

    int screen_w = fb_get_width();
    int screen_h = fb_get_height();
    int taskbar_y = screen_h - 36;

    bool current_mouse_left = (mouse_buttons & 1);
    if (current_mouse_left && !prev_mouse_left) {
        int btn_x = 4;
        int btn_y = taskbar_y + 4;
        int btn_w = 70;
        int btn_h = 28;

        if (right_menu_open) {
            int r_w = RIGHT_MENU_W;
            int r_h = RIGHT_MENU_H;
            int r_x = right_menu_x;
            int r_y = right_menu_y;

            if (r_x + r_w > screen_w) r_x = screen_w - r_w;
            if (r_y + r_h > screen_h) r_y = screen_h - r_h;

            if (new_x >= r_x + 4 && new_x <= r_x + r_w - 4 &&
                new_y >= r_y + 62 && new_y <= r_y + 86) {

                char target_path[256];
                const char *desktop_dir = "C:/Users/anil/Desktop";

                get_unique_filename(desktop_dir, "Metin Belgesi", ".txt", target_path, sizeof(target_path));
                
                if (vfs_create_file(target_path) == 0) {
                    damage_union_rect(0, 0, screen_w, screen_h);
                }
            }

            right_menu_open = false;
            damage_union_rect(0, 0, screen_w, screen_h);
            desktop_redraw();
        }
        else if (new_x >= btn_x && new_x <= btn_x + btn_w &&
                 new_y >= btn_y && new_y <= btn_y + btn_h) {
            start_menu_open = !start_menu_open;
            
            damage_union_rect(0, 0, screen_w, screen_h);
            desktop_redraw();
        }
        else if (start_menu_open) {
            int menu_x = 4;
            int menu_w = MENU_W; 
            int menu_h = MENU_H; 
            int menu_y = taskbar_y - menu_h;

            int shut_w = 100;
            int shut_h = 32;
            int shut_x = menu_x + menu_w - shut_w - 12;
            int shut_y = menu_y + menu_h - shut_h - 12;

            if (new_x >= shut_x && new_x <= shut_x + shut_w &&
                new_y >= shut_y && new_y <= shut_y + shut_h) {
                system_shutdown();
            }
            else {
                bool inside_menu = (new_x >= menu_x && new_x <= menu_x + menu_w &&
                                    new_y >= menu_y && new_y <= menu_y + menu_h);

                if (!inside_menu) {
                    start_menu_open = false;
                    damage_union_rect(0, 0, screen_w, screen_h);
                    desktop_redraw();
                }
            }
        }
    }
    prev_mouse_left = current_mouse_left;

    bool current_mouse_right = (mouse_buttons & 0x02);
    if (current_mouse_right && !prev_mouse_right) {
        if (new_y < taskbar_y) {
            right_menu_open = true;
            right_menu_x = new_x;
            right_menu_y = new_y;
            
            start_menu_open = false;

            damage_union_rect(0, 0, screen_w, screen_h);
            desktop_redraw();
        }
    }
    prev_mouse_right = current_mouse_right;

    if (old_x != new_x || old_y != new_y) {
        damage_union_rect(old_x, old_y, cursor_w, cursor_h);
        damage_union_rect(new_x, new_y, cursor_w, cursor_h);
        
        damage_union_rect(8, 8, 220, 24);
        
        if (right_menu_open) {
            damage_union_rect(right_menu_x, right_menu_y, RIGHT_MENU_W, RIGHT_MENU_H);
        }

        desktop_redraw();
    }
}