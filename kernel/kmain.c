#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <kernel/serial.h>
#include <kernel/multiboot.h>
#include <kernel/drivers/video/fb.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/fs/kryfs.h>
#include <kernel/drivers/input/mouse_ps2.h>
#include <ui/cursor.h>

// I/O port okumak için dışarıdan erişim (veya mouse_ps2.h içinde tanımlı olmalı)
static inline uint8_t inb_port(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void kernel_main(uint32_t mboot_magic, uint32_t* mboot_info_addr) {
    serial_init();
    serial_write("KryonOS baslatildi!\n");

    if (mboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        serial_write("HATA: Gecersiz magic number!\n");
        while (1) { __asm__ volatile("hlt"); }
    }

    multiboot_info_t* mboot = (multiboot_info_t*) mboot_info_addr;

    // Sistem bileşenlerini başlat
    fb_init(mboot);
    ata_init();
    kryfs_init();
    mouse_init(); 

    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    if (width > 0 && height > 0) {
        fb_clear(0xFF0000FF); // Mavi ekran (Masaüstü duvar kağıdı niyetine)
        desktop_init();       // Görev çubuğunu çiz
    }

    // İmleci en son başlatıyoruz ki görev çubuğunun üzerine doğru konumda gelsin
    cursor_init();

    // Sürekli fare verilerini yokla (Polling loop)
    while (1) {
        uint8_t status = inb_port(0x64);
        if (status & 1) {
            if (status & 0x20) {
                // Veri fareden geliyor
                mouse_handler();
                cursor_update();
            } else {
                // Veri klavyeden geliyor - tamponu temizlemek için mutlaka okuyup boşaltmalıyız
                volatile uint8_t dummy = inb_port(0x60);
                (void)dummy;
            }
        }
    }
}