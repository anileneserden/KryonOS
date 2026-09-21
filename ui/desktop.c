#include <ui/desktop.h>
#include <ui/wm.h>
#include <ui/cursor.h>
#include <ui/grid.h> // Grid header
#include <kernel/drivers/video/fb.h>
#include <kernel/drivers/video/gfx.h>
#include <kernel/drivers/input/keyboard_ps2.h>
#include <arch/x86/io.h>
#include <kernel/power.h>

#define MENU_W 400
#define MENU_H 500

extern uint8_t mouse_buttons;

typedef struct {
    int x, y, w, h;
    bool active;
} damage_rect_t;

static damage_rect_t screen_damage = {0, 0, 0, 0, false};

// Start menu open/closed state and previous mouse left click state
static bool start_menu_open = false;
static bool prev_mouse_left = false;

static bool right_menu_open = false;
static bool prev_mouse_right = false;
static int right_menu_x = 0;
static int right_menu_y = 0;

#define RIGHT_MENU_W 140
#define RIGHT_MENU_H 90

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

    // 1. Remove the old cursor first (restore the pixels beneath it)
    cursor_prepare_redraw();

    // 2. Desktop background
    gfx_fill_rect(screen_damage.x, screen_damage.y, screen_damage.w, screen_damage.h, 0xFF1E1E1E);

    // 3. Grid lines
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
        if (y >= screen_damage.y && y <= screen_damage.y + screen_damage.w) { 
            gfx_fill_rect(screen_damage.x, y, screen_damage.w, 1, grid_line_color);
        }
    }

    // 4. Draw open windows
    wm_draw_all();

    // 5. Bottom taskbar - draw it on the topmost layer (above windows)
    int taskbar_h = 36;
    int taskbar_y = screen_h - taskbar_h;
    if (screen_damage.y + screen_damage.h >= taskbar_y) {
        // Taskbar background (dark gray / black tone)
        gfx_fill_rect(screen_damage.x, taskbar_y > screen_damage.y ? taskbar_y : screen_damage.y, 
                      screen_damage.w, taskbar_h, 0xFF181818);
        // Taskbar top border (a thin light line for a modern border effect)
        gfx_fill_rect(screen_damage.x, taskbar_y, screen_damage.w, 1, 0xFF333333);

        // --- START BUTTON ---
        int btn_x = 4;
        int btn_y = taskbar_y + 4;
        int btn_w = 70;
        int btn_h = 28;

        // Show the start button pressed (dark) when menu is open
        uint32_t btn_bg = start_menu_open ? 0xFF2A2A2A : 0xFF3A3A3A;
        gfx_fill_rect(btn_x, btn_y, btn_w, btn_h, btn_bg);
        
        // Button border (for a classic 3D look and feel)
        gfx_fill_rect(btn_x, btn_y, btn_w, 1, 0xFF555555); // Top
        gfx_fill_rect(btn_x, btn_y, 1, btn_h, 0xFF555555); // Left
        gfx_fill_rect(btn_x + btn_w - 1, btn_y, 1, btn_h, 0xFF111111); // Right
        gfx_fill_rect(btn_x, btn_y + btn_h - 1, btn_w, 1, 0xFF111111); // Bottom
    }

    // 6. --- START MENU (Drawn if open) ---
    if (start_menu_open) {
        int menu_x = 4;
        int menu_w = MENU_W;
        int menu_h = MENU_H;
        int menu_y = taskbar_y - menu_h;

        // Menu main background
        gfx_fill_rect(menu_x, menu_y, menu_w, menu_h, 0xFF222222);

        // Classic Windows-style side strip (Gray decorative panel)
        gfx_fill_rect(menu_x, menu_y, 24, menu_h, 0xFF333333);

        // --- SHUTDOWN BUTTON (Aligned to bottom-right with text added) ---
        int shut_w = 100; // Button width
        int shut_h = 32;  // Button height
        int shut_x = menu_x + menu_w - shut_w - 12; // 12px inside from the right of the menu
        int shut_y = menu_y + menu_h - shut_h - 12; // 12px above from the bottom of the menu

        // Button background (Reddish / Dark tone)
        gfx_fill_rect(shut_x, shut_y, shut_w, shut_h, 0xFF4A2222);
        // Button 3D border
        gfx_fill_rect(shut_x, shut_y, shut_w, 1, 0xFF663333); // Top
        gfx_fill_rect(shut_x, shut_y, 1, shut_h, 0xFF663333); // Left
        gfx_fill_rect(shut_x + shut_w - 1, shut_y, 1, shut_h, 0xFF221111); // Right
        gfx_fill_rect(shut_x, shut_y + shut_h - 1, shut_w, 1, 0xFF221111); // Bottom

        // Button Text ("Shut Down" - White color)
        gfx_draw_text(shut_x + 22, shut_y + 8, 0xFFFFFFFF, "Shut Down");

        // Menu outer border
        gfx_fill_rect(menu_x, menu_y, menu_w, 1, 0xFF666666); // Top
        gfx_fill_rect(menu_x, menu_y, 1, menu_h, 0xFF666666); // Left
        gfx_fill_rect(menu_x + menu_w - 1, menu_y, 1, menu_h, 0xFF111111); // Right
        gfx_fill_rect(menu_x, menu_y + menu_h - 1, menu_w, 1, 0xFF111111); // Bottom
    }

    // 6.5. --- RIGHT-CLICK MENU (Drawn if open and Hover Check is Performed) ---
    if (right_menu_open) {
        int r_w = RIGHT_MENU_W;
        int r_h = RIGHT_MENU_H;
        int r_x = right_menu_x;
        int r_y = right_menu_y;

        // Prevent overflowing screen boundaries
        if (r_x + r_w > screen_w) r_x = screen_w - r_w;
        if (r_y + r_h > screen_h) r_y = screen_h - r_h;

        // Menu background
        gfx_fill_rect(r_x, r_y, r_w, r_h, 0xFF222222);

        int cur_x = cursor_get_x();
        int cur_y = cursor_get_y();

        // Option 1: Refresh
        bool hover_yenile = (cur_x >= r_x + 4 && cur_x <= r_x + r_w - 4 &&
                             cur_y >= r_y + 6 && cur_y <= r_y + 30);
        uint32_t bg_yenile = hover_yenile ? 0xFF3A3A3A : 0xFF2A2A2A; // Lighter color if hovered
        
        gfx_fill_rect(r_x + 4, r_y + 6, r_w - 8, 24, bg_yenile);
        gfx_draw_text(r_x + 12, r_y + 10, 0xFFFFFFFF, "Refresh");

        // Option 2: Terminal
        bool hover_terminal = (cur_x >= r_x + 4 && cur_x <= r_x + r_w - 4 &&
                               cur_y >= r_y + 34 && cur_y <= r_y + 58);
        uint32_t bg_terminal = hover_terminal ? 0xFF3A3A3A : 0xFF2A2A2A; // Lighter color if hovered

        gfx_fill_rect(r_x + 4, r_y + 34, r_w - 8, 24, bg_terminal);
        gfx_draw_text(r_x + 12, r_y + 38, 0xFFFFFFFF, "Terminal");

        // Menu Outer Border (Classic 3D look)
        gfx_fill_rect(r_x, r_y, r_w, 1, 0xFF666666); // Top
        gfx_fill_rect(r_x, r_y, 1, r_h, 0xFF666666); // Left
        gfx_fill_rect(r_x + r_w - 1, r_y, 1, r_h, 0xFF111111); // Right
        gfx_fill_rect(r_x, r_y + r_h - 1, r_w, 1, 0xFF111111); // Bottom
    }

    // 7. Draw the cursor at its new back-buffer position (blit=false because we perform one combined blit)
    cursor_show_internal(false);

    // 8. Blit the entire damaged region to the screen
    fb_blit_region(screen_damage.x, screen_damage.y, screen_damage.w, screen_damage.h);
    
    damage_clear();
}

void desktop_init(void) {
    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    if (width == 0 || height == 0) return;

    // Initialize the grid here using the screen dimensions (for example, 64x64 cells)
    grid_init(width, height, 100, 100);

    wm_init();

    damage_union_rect(0, 0, width, height);
    desktop_redraw();

    cursor_init();
    cursor_sync_position();
    cursor_show();
    cursor_refresh_background();
}

void desktop_process_input(void) {
    // 1. Get the previous cursor position (if the cursor module stores it)
    int old_x = cursor_get_old_x();
    int old_y = cursor_get_old_y();

    // 2. Process input and window events (mouse/keyboard positions are updated)
    wm_process_input();

    int new_x = cursor_get_x();
    int new_y = cursor_get_y();
    int cursor_w = cursor_get_width();   
    int cursor_h = cursor_get_height();  

    int screen_w = fb_get_width();
    int screen_h = fb_get_height();
    int taskbar_y = screen_h - 36;

    // 3. Left Click Check (Detection of clicks on start button, menu, and shutdown button)
    bool current_mouse_left = (mouse_buttons & 1);
    if (current_mouse_left && !prev_mouse_left) {
        int btn_x = 4;
        int btn_y = taskbar_y + 4;
        int btn_w = 70;
        int btn_h = 28;

        // If a left-click occurs while the right-click menu is open, close it first
        if (right_menu_open) {
            right_menu_open = false;
            damage_union_rect(0, 0, screen_w, screen_h);
            desktop_redraw();
        }

        // Is the mouse over the Start button?
        if (new_x >= btn_x && new_x <= btn_x + btn_w &&
            new_y >= btn_y && new_y <= btn_y + btn_h) {
            start_menu_open = !start_menu_open;
            
            // Mark the entire screen as damaged and trigger a redraw since the menu will open/close
            damage_union_rect(0, 0, screen_w, screen_h);
            desktop_redraw();
        }
        // Inside or outside menu check when menu is open
        else if (start_menu_open) {
            int menu_x = 4;
            int menu_w = MENU_W; 
            int menu_h = MENU_H; 
            int menu_y = taskbar_y - menu_h;

            // Shutdown button coordinates (Must be identical to the drawing)
            int shut_w = 100;
            int shut_h = 32;
            int shut_x = menu_x + menu_w - shut_w - 12;
            int shut_y = menu_y + menu_h - shut_h - 12;

            // Did the user click the Shutdown button?
            if (new_x >= shut_x && new_x <= shut_x + shut_w &&
                new_y >= shut_y && new_y <= shut_y + shut_h) {
                system_shutdown(); // Shut down the system
            }
            else {
                // Close the menu if clicked outside
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

    // 4. --- RIGHT-CLICK CHECK ---
    bool current_mouse_right = (mouse_buttons & 0x02); // Bit 1 = Right Click
    if (current_mouse_right && !prev_mouse_right) {
        // Open the menu if the desktop is right-clicked outside the taskbar area
        if (new_y < taskbar_y) {
            right_menu_open = true;
            right_menu_x = new_x;
            right_menu_y = new_y;
            
            // Close the start menu to avoid conflicts
            start_menu_open = false;

            damage_union_rect(0, 0, screen_w, screen_h);
            desktop_redraw();
        }
    }
    prev_mouse_right = current_mouse_right;

    // 5. If the cursor moved, mark its old and new positions as damaged
    if (old_x != new_x || old_y != new_y) {
        damage_union_rect(old_x, old_y, cursor_w, cursor_h); // Clear the old position
        damage_union_rect(new_x, new_y, cursor_w, cursor_h); // Draw the new position
        
        // Ensure the menu also refreshes when the mouse moves while the right-click menu is open (for hover)
        if (right_menu_open) {
            damage_union_rect(right_menu_x, right_menu_y, RIGHT_MENU_W, RIGHT_MENU_H);
        }

        // Trigger a redraw
        desktop_redraw();
    }
}