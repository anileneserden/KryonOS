#include <ui/desktop_icons.h>
#include <ui/grid.h>
#include <kernel/drivers/video/fb.h>
#include <kernel/drivers/video/gfx.h>
#include <kernel/fs/vfs.h>
#include <kernel/serial.h>
#include <kernel/string.h>
#include <stddef.h>

extern uint32_t desktop_bg_color;
void damage_union_rect(int x, int y, int w, int h);

static int selected_icon_index = -1;
static int last_clicked_index = -1;
static uint32_t last_click_tick = 0;
static uint32_t global_tick_counter = 0;

static int last_hover_index = -1;

void desktop_icons_init(void) {
    selected_icon_index = -1;
    last_clicked_index = -1;
    last_click_tick = 0;
    global_tick_counter = 0;
    last_hover_index = -1;
}

int desktop_icons_get_selected(void) {
    return selected_icon_index;
}

// Metin genişliği kutuyu aşıyorsa metnin sonuna '...' koyarak kısaltır
static void truncate_text(const char *src, char *dst, size_t max_dst, int max_chars) {
    int len = strlen(src);
    if (len <= max_chars) {
        strncpy(dst, src, max_dst);
        dst[max_dst - 1] = '\0';
    } else {
        if (max_chars > 3) {
            strncpy(dst, src, max_chars - 3);
            dst[max_chars - 3] = '\0';
            strcat(dst, "...");
        } else {
            strncpy(dst, src, max_chars);
            dst[max_chars] = '\0';
        }
    }
}

void desktop_icons_draw(int32_t mx, int32_t my, bool click_started) {
    (void)click_started;

    vfs_file_info_t files[16];
    int file_count = vfs_get_directory_files("C:/Users/anil/Desktop", files, 16);

    int cell_w = grid_get_cell_width();  // 100 px
    int cell_h = grid_get_cell_height(); // 100 px

    int current_hover_index = -1;

    // 1. Mouse'un hangi ikon hücresinde olduğunu tespit et
    for (int i = 0; i < file_count; i++) {
        int gx = 0, gy = 0;
        grid_get_position(i, &gx, &gy);

        if (mx >= gx && mx < (gx + cell_w) &&
            my >= gy && my < (gy + cell_h)) {
            current_hover_index = i;
            break;
        }
    }

    // 2. Hover değişimi takibi (Damage yönetimi)
    if (current_hover_index != last_hover_index) {
        if (last_hover_index != -1 && last_hover_index < file_count) {
            int prev_gx = 0, prev_gy = 0;
            grid_get_position(last_hover_index, &prev_gx, &prev_gy);
            damage_union_rect(prev_gx, prev_gy, cell_w, cell_h);
        }

        if (current_hover_index != -1) {
            int new_gx = 0, new_gy = 0;
            grid_get_position(current_hover_index, &new_gx, &new_gy);
            damage_union_rect(new_gx, new_gy, cell_w, cell_h);
        }

        last_hover_index = current_hover_index;
    }

    // 3. İkonları Çiz
    for (int i = 0; i < file_count; i++) {
        int gx = 0, gy = 0;
        grid_get_position(i, &gx, &gy);

        // Grid hücresine göre sınırlandırılmış hover/seçim kutusu (Örn: 88x88px)
        int box_w = 88;
        int box_h = 88;
        
        int box_x = gx + (cell_w - box_w) / 2;
        int box_y = gy + (cell_h - box_h) / 2;

        bool is_hovered = (i == current_hover_index);
        uint32_t bg_color = is_hovered ? 0xFF3A4D6B : desktop_bg_color;

        // Hover veya zemin temizliği için kutuyu doldur
        gfx_fill_rect(box_x, box_y, box_w, box_h, bg_color);

        // --- İKONU KUTU İÇİNDE ORTALAMA ---
        int icon_w = 36;
        int icon_h = 36;
        int icon_x = box_x + (box_w - icon_w) / 2;
        int icon_y = box_y + 8;

        gfx_fill_rect(icon_x, icon_y, icon_w, icon_h, 0xFFCCCCCC);
        gfx_fill_rect(icon_x + 3, icon_y + 3, icon_w - 6, icon_h - 6, 0xFFFFFFFF);
        gfx_fill_rect(icon_x + 7, icon_y + 9, 14, 2, 0xFF555555);
        gfx_fill_rect(icon_x + 7, icon_y + 15, 18, 2, 0xFF555555);

        // --- METNİ KISALTMA VE ORTALAMA ---
        char display_name[32];
        int char_width = 8;
        // Kutunun içine sığabilecek maksimum karakter sayısı (sağdan/soldan 4px padding bırakarak)
        int max_chars = (box_w - 8) / char_width; // 80 / 8 = 10 karakter

        truncate_text(files[i].name, display_name, sizeof(display_name), max_chars);

        int text_w = strlen(display_name) * char_width;
        int text_x = box_x + (box_w - text_w) / 2;
        int text_y = icon_y + icon_h + 8;

        gfx_draw_text_utf8(text_x, text_y, 0xFFFFFFFF, display_name);
    }
}