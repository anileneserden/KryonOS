#include <ui/desktop_icons.h>
#include <kernel/drivers/video/fb.h>
#include <kernel/drivers/video/gfx.h>
#include <kernel/fs/vfs.h>
#include <kernel/serial.h>
#include <stddef.h>

static int selected_icon_index = -1;
static int last_clicked_index = -1;
static uint32_t last_click_tick = 0;
static uint32_t global_tick_counter = 0;

void desktop_icons_init(void) {
    selected_icon_index = -1;
    last_clicked_index = -1;
    last_click_tick = 0;
    global_tick_counter = 0;
}

int desktop_icons_get_selected(void) {
    return selected_icon_index;
}

void desktop_icons_draw(int32_t mx, int32_t my, bool click_started) {
    global_tick_counter++;

    vfs_file_info_t files[16];
    int file_count = vfs_get_directory_files("C:/Users/anil/Desktop/", files, 16);

    // If a new click started, handle icon selection and double-click detection
    if (click_started) {
        int clicked_index = -1;
        int check_x = 30;
        int check_y = 30;
        for (int i = 0; i < file_count; i++) {
            int box_w = 64;
            int box_h = 64;
            int render_x = check_x - 12;
            int render_y = check_y - 4;

            if (mx >= render_x && mx < (render_x + box_w) &&
                my >= render_y && my < (render_y + box_h)) {
                clicked_index = i;
                break;
            }
            check_y += 70;
        }

        selected_icon_index = clicked_index; 

        if (clicked_index != -1) {
            if (clicked_index == last_clicked_index && (global_tick_counter - last_click_tick) < 45) {
                const char* filename = files[clicked_index].name;
                
                // Find a dot to perform a simple extension check
                const char *ext = NULL;
                for (const char *p = filename; *p != '\0'; p++) {
                    if (*p == '.') ext = p;
                }

                serial_write("Double-click detected: icon ");
                serial_write(filename);

                if (ext) {
                    // For example, use the notepad handler for .txt files
                    if (ext[1] == 't' && ext[2] == 'x' && ext[3] == 't' && ext[4] == '\0') {
                        serial_write(" -> Type: Text Document (Notepad Handler)");
                    } else {
                        serial_write(" -> Type: Unknown Extension");
                    }
                } else {
                    // Files without an extension or direct kernel/executable files such as .kef
                    serial_write(" -> Type: KEF / Kernel Module / Executable");
                }
                serial_write("\n");

                last_clicked_index = -1;
                last_click_tick = 0;
            } else {
                last_clicked_index = clicked_index;
                last_click_tick = global_tick_counter;
            }
        } else {
            last_clicked_index = -1;
        }
    }

    int icon_x = 30;
    int icon_y = 30;

    for (int i = 0; i < file_count; i++) {
        int box_w = 64;
        int box_h = 64;
        int render_x = icon_x - 12;
        int render_y = icon_y - 4;

        bool is_hovered = (mx >= render_x && mx < (render_x + box_w) &&
                           my >= render_y && my < (render_y + box_h));
        bool is_selected = (selected_icon_index == i);

        if (is_selected) {
            gfx_fill_rect_alpha(render_x, render_y, box_w, box_h, 0x00FFFFFF, 150);
        } else if (is_hovered) {
            gfx_fill_rect_alpha(render_x, render_y, box_w, box_h, 0x00FFFFFF, 85);
        }

        // Draw the icon box and icon
        gfx_fill_rect(icon_x, icon_y, 40, 40, 0xFFE0E0E0);
        gfx_fill_rect(icon_x + 4, icon_y + 4, 32, 28, 0xFFFFFFFF);

        // File name label
        gfx_draw_text_utf8(icon_x - 4, icon_y + 45, 0xFFFFFFFF, files[i].name);

        icon_y += 70;
    }
}