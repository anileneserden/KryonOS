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

// I/O port okumak için dışarıdan erişim
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

    // --- FİZİKSEL BELLEK YÖNETİCİSİNİ BAŞLAT ---
    pmm_init(mboot);
    vmm_init();
    heap_init(0x600000, 0x1000000);

    // Sistem bileşenlerini başlat
    fb_init(mboot);
    ata_init();

    // --- VFS VE DOSYA SİSTEMİNİ BAŞLAT ---
    vfs_init();
    kryos_fs_system_init(); // KRYFS'yi başlatır ve C:\ olarak mount eder

    // Sürücü içeriğini Windows tarzı liste olarak göster
    vfs_list_drive('C');

    // Test: VFS üzerinden C:\ harfiyle dosya okuma
    uint32_t fsize = 0;
    char* fdata = (char*)vfs_read_file("C:/test.txt", &fsize);
    if (fdata && fsize > 0) {
        serial_write("VFS Uzerinden 'C:/test.txt' Basariyla Okundu:\n[ ");
        for (uint32_t i = 0; i < fsize; i++) {
            char c[2] = { fdata[i], '\0' };
            serial_write(c);
        }
        serial_write(" ]\n");
    } else {
        serial_write("VFS Uzerinden 'C:/test.txt' Okunamadi!\n");
    }
    
    mouse_init(); 

    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    if (width > 0 && height > 0) {
        fb_clear(0xFF0000FF); // Mavi ekran arka planı
        desktop_init();       // Görev çubuğu ve pencereler
    }

    // İmleci başlangıç konumuna hazırla ve ilk tam ekranı yansıt
    cursor_init();
    
    // İlk çizimi ekrana komple basıyoruz
    // (Not: cursor_init arka plandan okuduğu için önce masaüstü buffer'da olmalı)
    // İmleci ilk konumuna back_buffer üzerine çizip tam ekran swap yapabiliriz:
    // Ya da doğrudan fb_swap() çağırabilirsin:
    fb_swap();

    // Sürekli giriş ve render döngüsü
    while (1) {
        // Girdi kontrolü (non-blocking)
        if (inb_port(0x64) & 1) {
            uint8_t status = inb_port(0x64);
            if (status & 0x20) {
                mouse_handler();

                // Fare hareket ettiğinde veya tıklandığında pencere yöneticisini çalıştır
                wm_process_input();

                cursor_update_and_redraw(); // Sadece imleç karesini günceller
            } else {
                keyboard_handler();
            }
        }
    }
}