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
#include <kernel/audio/wav.h>
#include <kernel/drivers/input/mouse_ps2.h>
#include <kernel/drivers/input/keyboard_ps2.h>
#include <kernel/drivers/pci.h>
#include <kernel/drivers/usb/uhci.h>
#include <ui/cursor.h>
#include <ui/desktop.h>
#include <kernel/mem/heap.h>
#include <kernel/mem/pmm.h>
#include <kernel/mem/vmm.h>
#include <kernel/app.h>
#include <kernel/kef.h>
#include <arch/x86/io.h>

static inline uint8_t inb_port(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void sample_app_draw(app_t* app) {
    (void)app; // Avoid the -Wunused-parameter warning
}

void kernel_main(uint32_t mboot_magic, uint32_t* mboot_info_addr) {
    serial_init();
    serial_write("KryonOS started!\n");

    if (mboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        serial_write("ERROR: Invalid magic number!\n");
        while (1) { __asm__ volatile("hlt"); }
    }

    multiboot_info_t* mboot = (multiboot_info_t*) mboot_info_addr;

    // 1. Memory management
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
    kryos_fs_system_init();
    
    if (fat32_init_disk(1, 0)) {
        fs_driver_t fat32_driver = fat32_get_driver();
        vfs_mount('D', "FAT32_VOL", fat32_driver);
    } else {
        serial_write("FAT32: Driver could not be started!\n");
    }

    if (!uhci_mouse_active()) {
        mouse_init();
    } else {
        serial_write("USB mouse active; PS/2 mouse initialization skipped.\n");
    }
    keyboard_init();

    // --- C:/Users/anil/Desktop/test.txt DOSYASINI OKUMA VE SERIAL'A YAZMA ---
    uint32_t file_size = 0;
    char* file_content = (char*)vfs_read_file("C:/Users/anil/Desktop/test.txt", &file_size);
    
    if (file_content && file_size > 0) {
        serial_write("\n[VFS] C:/test.txt basariyla okundu:\n--- BASLANGIC ---\n");
        
        // Karakter karakter veya blok halinde serial porta yazdır
        for (uint32_t i = 0; i < file_size; i++) {
            char c[2] = { file_content[i], '\0' };
            serial_write(c);
        }
        
        serial_write("\n--- BITIS ---\n\n");
    } else {
        serial_write("[VFS HATA] test.txt okunamadi veya dosya bos!\n");
    }
    // --------------------------------------------------------

    // 4. Grafik Arayüzünün Başlatılması ve İlk Çizim
    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    if (width > 0 && height > 0) {
        fb_clear(0xFF0000FF); 
        desktop_init(); 
    }

    app_manager_init();
    // app_create("Not Defteri", 250, 180, 0, sample_app_draw);

    fb_swap();

    // 5. AC97 audio driver and WAV player
    if (ac97_init() == 0) {
        ac97_set_master_volume(100);

        serial_write("AC97: Attempting to play startup.wav...\n");
        wav_play_file("C:/Kryon/Media/startup.wav");
    }

    // 6. Main event loop
    while (1) {
        uint8_t input_updated = uhci_poll();
        if (inb_port(0x64) & 1) {
            input_updated = 1;
            uint8_t status = inb_port(0x64);
            if (status & 0x20) {
                mouse_handler();
            } else {
                keyboard_handler();
            }
        }

        if (input_updated) {
            desktop_process_input();
            app_manager_update_all();
        }

        cursor_update_and_redraw();
    }
}