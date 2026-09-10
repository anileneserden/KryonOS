#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <kernel/serial.h>
#include <kernel/multiboot.h>
#include <kernel/drivers/video/fb.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/fs/kryfs.h>
#include <kernel/fs/vfs.h>
#include <kernel/drivers/input/mouse_ps2.h>
#include <kernel/drivers/input/keyboard_ps2.h>
#include <ui/cursor.h>
#include <ui/desktop.h>
#include <kernel/mem/heap.h>
#include <kernel/mem/pmm.h>
#include <kernel/mem/vmm.h>
#include <kernel/app.h>

// I/O port okumak için dışarıdan erişim
static inline uint8_t inb_port(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Örnek bir test uygulaması çizim fonksiyonu (İsteğe bağlı özel içerik için)
void sample_app_draw(app_t* app) {
    // Pencere içerisine özel çizimler yapabilirsin
}

void kernel_main(uint32_t mboot_magic, uint32_t* mboot_info_addr) {
    serial_init();
    serial_write("KryonOS baslatildi!\n");

    if (mboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        serial_write("HATA: Gecersiz magic number!\n");
        while (1) { __asm__ volatile("hlt"); }
    }

    multiboot_info_t* mboot = (multiboot_info_t*) mboot_info_addr;

    // --- FİZİKSEL BELLEK YÖNETİCİSİ VE HEAP ---
    pmm_init(mboot);
    vmm_init();
    heap_init(0x600000, 0x1000000);

    // Sistem bileşenleri
    fb_init(mboot);
    ata_init();
    vfs_init();
    kryos_fs_system_init();

    mouse_init(); 

    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    if (width > 0 && height > 0) {
        fb_clear(0xFF0000FF); 
        desktop_init();       
    }

    // --- APP MANAGER'I BAŞLAT VE İLK UYGULAMAYI AÇ ---
    app_manager_init();
    
    // Test amaçlı ilk uygulamamızı / pencereyi app_manager üzerinden oluşturuyoruz:
    app_create("Not Defteri", 250, 180, 0, sample_app_draw);

    cursor_init();
    fb_swap();

    // Ana döngü
    while (1) {
        if (inb_port(0x64) & 1) {
            uint8_t status = inb_port(0x64);
            if (status & 0x20) {
                mouse_handler();
                wm_process_input();
                app_manager_update_all(); // Uygulama döngülerini burada tetikleyebiliriz
                cursor_update_and_redraw();
            } else {
                keyboard_handler();
            }
        }
    }
}