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

static inline uint8_t inb_port(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void kernel_main(uint32_t mboot_magic, uint32_t* mboot_info_addr) {
    serial_init();
    serial_write("KryonOS started!\n");

    if (mboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        serial_write("ERROR: Invalid magic number!\n");
        return;
    }

    multiboot_info_t* mboot = (multiboot_info_t*)mboot_info_addr;

    // 1. Memory and heap initialization
    pmm_init(mboot);
    vmm_init();
    heap_init(0x2000000, 1024 * 1024 * 16);

    // 2. PCI and display hardware drivers
    pci_init();
    uhci_init();
    fb_init(mboot);
    ata_init();

    // 3. Filesystems and input drivers
    vfs_init();
    
    if (fat32_init_disk(1, 0)) {
        fs_driver_t fat32_driver = fat32_get_driver();
        vfs_mount('D', "FAT32_VOL", fat32_driver);
    } else {
        serial_write("FAT32: Driver could not be started!\n");
    }

    // KRYFS Mount ve VFS Entegrasyonu (C Sürücüsü)
    if (kryfs_mount() == 0) {
        fs_driver_t kryfs_driver = kryfs_get_driver();
        vfs_mount('C', "KRYFS_VOL", kryfs_driver);
        serial_write("KRYFS mounted successfully and registered to VFS on C:\\.\n");
    } else {
        serial_write("KRYFS: Driver could not be started!\n");
    }

    // --- KRYFS / VFS DOSYA OKUMA TESTİ ---
    uint32_t test_size = 0;
    void* file_data = vfs_read_file("C:/test.txt", &test_size);
    
    serial_write("---- test.txt Dosya İçeriği ----\n");
    if (file_data && test_size > 0) {
        serial_write((char*)file_data);
        serial_write("\n");
        kfree(file_data);
    } else {
        serial_write("[HATA] test.txt okunamadi veya bulunamadi!\n");
    }
    serial_write("--------------------------------\n");
    // ------------------------------------

    // 4. Input drivers
    if (!uhci_mouse_active()) {
        mouse_init();
    } else {
        serial_write("USB mouse active; PS/2 mouse initialization skipped.\n");
    }
    keyboard_init();

    // 5. Grafik Arayüzünün Başlatılması ve İlk Çizim
    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    if (width > 0 && height > 0) {
        fb_clear(0xFF0000FF); // Lacivert/Mavi masaüstü arka planı
        desktop_init(); 
    }

    serial_write("---- KEF Uygulamasi Baslatiliyor ----\n");
    // (x86) ifadesini kaldırarak doğru klasör yolunu veriyoruz:
    if (kef_load_and_run("C:/Program Files/calculator/calculator.kef")) {
        serial_write("KEF: Uygulama basariyla calistirildi ve sonlandi.\n");
    } else {
        serial_write("[HATA] KEF uygulamasi baslatilamadi!\n");
    }
    serial_write("-------------------------------------\n");

    app_manager_init();
    fb_swap();

    // 6. AC97 audio driver initialization
    if (ac97_init() == 0) {
        ac97_set_master_volume(100);
        serial_write("AC97: Driver initialized and ready.\n");
    }

    // 7. Main event loop (GUI aktif döngü)
    while (1) {
        uint8_t input_updated = uhci_poll();
        if (inb_port(0x64) & 1) {
            keyboard_handler();
            input_updated = 1;
        }

        if (input_updated) {
            desktop_process_input();
            app_manager_update_all();
        }

        cursor_update_and_redraw();
    }
}