#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <kernel/serial.h>
#include <kernel/multiboot.h>
#include <kernel/drivers/video/fb.h>
#include <kernel/drivers/storage/ata.h>
#include <kernel/fs/kryfs.h>
#include <kernel/fs/fat32.h>
#include <kernel/fs/vfs.h>
#include <kernel/drivers/audio/ac97.h>
#include <kernel/drivers/audio/pcspeaker.h>
#include <kernel/drivers/input/mouse_ps2.h>
#include <kernel/drivers/input/keyboard_ps2.h>
#include <kernel/drivers/pci.h>
#include <kernel/drivers/usb/uhci.h>
#include <ui/cursor.h>
#include <ui/desktop.h>
#include <kernel/app.h>
#include <kernel/mem/heap.h>
#include <kernel/mem/pmm.h>
#include <kernel/mem/vmm.h>
#include <arch/x86/io.h>
#include <kernel/hexdump.h>
#include <kernel/kef.h>

void kernel_main(uint32_t mboot_magic, uint32_t* mboot_info_addr) {
    serial_init();
    serial_write("KryonOS started!\n");

    if (mboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        serial_write("ERROR: Invalid magic number!\n");
        return;
    }

    multiboot_info_t* mboot = (multiboot_info_t*)mboot_info_addr;

    // 1. Bellek Yönetimi
    pmm_init(mboot);
    vmm_init();
    heap_init(0x2000000, 1024 * 1024 * 16);

    // 2. Temel Donanım Sürücüleri
    pci_init();
    uhci_init();
    fb_init(mboot);

    // --- TEST 1: KIRMIZI (FB Sürücüsü Çalışıyor) ---
    fb_clear(0xFFFF0000); 
    fb_swap();

    ata_init();

    // 3. Dosya Sistemleri
    vfs_init();
    
    if (fat32_init_disk(1, 0)) {
        fs_driver_t fat32_driver = fat32_get_driver();
        vfs_mount('D', "FAT32_VOL", fat32_driver);
    }

    if (kryfs_mount() == 0) {
        fs_driver_t kryfs_driver = kryfs_get_driver();
        vfs_mount('C', "KRYFS_VOL", kryfs_driver);
    }

    // --- TEST 2: SARI (Dosya Sistemleri Başarıyla Yüklendi) ---
    fb_clear(0xFFFFFF00); 
    fb_swap();

    // 4. Girdi Aygıtları
    if (!uhci_mouse_active()) {
        mouse_init();
    }
    keyboard_init();

    // --- TEST 3: MAVİ (Girdi Aygıtları Hazır) ---
    fb_clear(0xFF0000FF); 
    fb_swap();

    // 5. Masaüstü ve Uygulama Yönetimi
    desktop_init(); 

    // --- TEST 4: YEŞİL (Masaüstü İlklendi, App Manager Geçiliyor) ---
    fb_clear(0xFF00FF00); 
    fb_swap();

    app_manager_init();

    // AC97 Başlatma
    if (ac97_init() == 0) {
        ac97_set_master_volume(100);
    }

    // İlk Masaüstü Çizimi
    fb_clear(0xFF0000FF); // Standart Mavi Arka Plan
    desktop_process_input(); // Arayüzü ilk duruma getir
    fb_swap();

    // 6. Ana Olay Döngüsü
    while (1) {
        uint8_t input_updated = uhci_poll();

        if (inb(0x64) & 1) {
            keyboard_handler();
            input_updated = 1;
        }

        if (input_updated) {
            desktop_process_input();
            app_manager_update_all();
        }

        cursor_update_and_redraw();
        fb_swap();

        asm volatile("pause");
    }
}