#include <kernel/kef.h>
#include <kernel/fs/vfs.h>
#include <kernel/mem/heap.h>
#include <kernel/serial.h>
#include <kernel/string.h>
#include <ui/wm.h>
#include <ui/desktop.h>
#include <kernel/drivers/video/gfx.h>

// KEF payload'ının bellekteki gerçek başlangıç adresi
static uint8_t* current_payload_base = 0;

static int kef_window_create(const char* title, int width, int height) {
    // Offset adresini gerçek belleğe çevir
    char* real_title = (char*)(current_payload_base + (uint32_t)title);
    
    serial_write("KEF Window Title: ");
    serial_write(real_title);
    serial_write("\n");

    window_t* window = wm_create_window(width, height, real_title);
    return window != 0;
}

static void kef_serial_write(const char* str) {
    if (str) {
        // Offset adresini gerçek belleğe çevir
        char* real_str = (char*)(current_payload_base + (uint32_t)str);
        serial_write("[KEF App]: ");
        serial_write(real_str);
        serial_write("\n");
    }
}

static void kef_draw_text(int x, int y, const char* text, uint32_t color) {
    if (text) {
        char* real_text = (char*)(current_payload_base + (uint32_t)text);
        window_t* win = wm_get_active_window();
        
        if (win && win->width > 0) {
            // Pencere metin tamponuna kaydet
            wm_add_window_text(win, x, y, real_text, color);
            
            // Ekranı hasarlı işaretleyip yeniden çizilmesini sağla
            damage_union_rect(win->x, win->y, win->width, win->height);
            desktop_redraw();
        }
    }
}

static void kef_draw_rect(int x, int y, int w, int h, uint32_t color) {
    window_t* win = wm_get_active_window();
    if (win && win->width > 0) {
        wm_add_window_rect(win, x, y, w, h, color);
        damage_union_rect(win->x, win->y, win->width, win->height);
        desktop_redraw();
    }
}

static void kef_draw_button(int x, int y, int w, int h, const char* text, uint32_t color) {
    if (text) {
        char* real_text = (char*)(current_payload_base + (uint32_t)text);
        window_t* win = wm_get_active_window();
        
        if (win && win->width > 0) {
            wm_add_window_button(win, x, y, w, h, real_text, color);
            damage_union_rect(win->x, win->y, win->width, win->height);
            desktop_redraw();
        }
    }
}

static void kef_install_api(void) {
    volatile kef_api_t* api = (volatile kef_api_t*)KEF_API_ADDRESS;
    api->window_create = kef_window_create;
    api->serial_write = kef_serial_write;
    api->draw_text = kef_draw_text;
    api->draw_rect = kef_draw_rect;
    api->draw_button = kef_draw_button;
}

bool kef_load_and_run(const char* path) {
    uint32_t file_size = 0;
    uint8_t* file = (uint8_t*)vfs_read_file(path, &file_size);

    if (!file || file_size < sizeof(kef_header_t)) {
        serial_write("KEF: dosya okunamadi veya cok kucuk.\n");
        return false;
    }

    kef_header_t* header = (kef_header_t*)file;
    if (header->magic != KEF_MAGIC ||
        header->version != KEF_VERSION ||
        header->architecture != KEF_ARCH_I386 ||
        header->header_size < sizeof(kef_header_t) ||
        header->header_size > file_size ||
        header->payload_size > file_size - header->header_size ||
        header->entry_offset >= header->payload_size ||
        header->payload_size > KEF_MAX_SIZE) {
        serial_write("KEF: gecersiz veya desteklenmeyen baslik.\n");
        return false;
    }

    uint8_t* payload = (uint8_t*)kmalloc(header->payload_size);
    if (!payload) {
        serial_write("KEF: payload icin bellek ayrilamadi.\n");
        return false;
    }

    memcpy(payload, file + header->header_size, header->payload_size);

    // Payload taban adresini kaydet
    current_payload_base = payload;

    kef_install_api();
    serial_write("KEF: dosya yuklendi, entry cagriliyor.\n");
    kef_entry_t entry = (kef_entry_t)(payload + header->entry_offset);
    (void)entry();
    serial_write("KEF: uygulama geri dondu.\n");
    return true;
}