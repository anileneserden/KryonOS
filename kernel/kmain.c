// kernel/kmain.c
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
#include <ui/serial_screen.h>
#include <kernel/app.h>
#include <kernel/mem/heap.h>
#include <kernel/mem/pmm.h>
#include <kernel/mem/vmm.h>
#include <arch/x86/io.h>
#include <kernel/hexdump.h>
#include <kernel/loader/kef2.h>

// serial.c içerisindeki mirror fonksiyonunun prototipi
extern void serial_screen_mirror_enable(bool enable);

void kernel_main(uint32_t mboot_magic, uint32_t* mboot_info_addr) {
    serial_init();
    serial_write("KryonOS started!\n");

    if (mboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        serial_write("ERROR: Invalid magic number!\n");
        return;
    }

    multiboot_info_t* mboot = (multiboot_info_t*)mboot_info_addr;

    bool debug_mode = false;
    if ((mboot->flags & (1 << 2)) && mboot->cmdline) {
        char* cmdline = (char*)mboot->cmdline;
        for (int i = 0; cmdline[i] != '\0'; i++) {
            if (cmdline[i] == 'd' && cmdline[i+1] == 'e' && cmdline[i+2] == 'b' && cmdline[i+3] == 'u' && cmdline[i+4] == 'g') {
                debug_mode = true;
                break;
            }
        }
    }

    // 1. Memory and heap initialization
    pmm_init(mboot);
    vmm_init();
    heap_init(0x2000000, 1024 * 1024 * 16);

    // Framebuffer başlatılıyor
    fb_init(mboot);

    // KLAVYE: Debug ekranı açılmadan hemen önce PS/2 klavyeyi başlatıyoruz ki portlar aktif olsun
    keyboard_init();

    if (debug_mode) {
        serial_screen_init();
        serial_screen_mirror_enable(true); // Tüm serial_write logları ekrana da akacak
        serial_screen_write("DEBUG MODE ACTIVE: Screen & Mirror initialized.\n");
    }

    // 2. PCI, display, and speaker hardware drivers
    pci_init();
    uhci_init();
    ata_init();

    // 3. Filesystems and input drivers
    //vfs_init();
    
    /*
    if (fat32_init_disk(1, 0)) {
        fs_driver_t fat32_driver = fat32_get_driver();
        vfs_mount('D', "FAT32_VOL", fat32_driver);
        serial_write("FAT32 mounted to D:\\\n");
    } else {
        serial_write("FAT32: Driver could not be started!\n");
    }*/

    /*
    // KRYFS Mount and VFS Integration (C Drive)
    if (kryfs_mount() == 0) {
        fs_driver_t kryfs_driver = kryfs_get_driver();
        vfs_mount('C', "KRYFS_VOL", kryfs_driver);
        serial_write("KRYFS mounted successfully and registered to VFS on C:\\.\n");
    } else {
        serial_write("KRYFS: Driver could not be started!\n");
    }*/

    /*
    // --- KRYFS / VFS FILE READ TEST ---
    uint32_t test_size = 0;
    void* file_data = vfs_read_file("C:/test.txt", &test_size);
    */

    /*
    serial_write("---- test.txt File Content ----\n");
    if (file_data && test_size > 0) {
        serial_write((char*)file_data);
        serial_write("\n");
        kfree(file_data);
    } else {
        serial_write("[ERROR] test.txt could not be read or found!\n");
    }
    serial_write("--------------------------------\n");
    */

    // 4. Input drivers status check
    if (uhci_mouse_active()) {
        serial_write("USB mouse active and ready.\n");
    } else {
        serial_write("WARNING: USB mouse not active yet, waiting for polling...\n");
    }

    // 5. Graphical Interface Initialization and Initial Drawing
    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    if (width > 0 && height > 0) {
        if (debug_mode) {
            serial_screen_write("Debug screen fully loaded. Press any key to test...\n");
            
            // hlt kaldırıldı: Tuşları anlık ve kesintisiz tarayan aktif polling döngüsü
            while (1) {
                if (inb(0x64) & 1) {
                    uint8_t scancode = inb(0x60);
                    serial_screen_handle_input(scancode);
                }
            }
        } else {
            fb_clear(0xFF0000FF);
            desktop_init(); 
        }
    }

    /*
    serial_write("---- Starting KEF Application ----\n");
    
    uint32_t kef_debug_size = 0;
    void* kef_debug_data = vfs_read_file("C:/Program Files/3drenderer/3drenderer.kef", &kef_debug_size);
    if (kef_debug_data && kef_debug_size > 0) {
        uint32_t dump_len = kef_debug_size > 1024 ? 1024 : kef_debug_size;
        kernel_hexdump(kef_debug_data, dump_len);
        kfree(kef_debug_data);
    } else {
        serial_write("[DEBUG ERROR] test4.kef could not be read for hexdump!\n");
    }*/

    app_manager_init();
    fb_swap();

    // 6. AC97 audio driver initialization
    if (ac97_init() == 0) {
        ac97_set_master_volume(100);
        serial_write("AC97: Driver initialized and ready.\n");
    }

    // 7. Main event loop (GUI active loop)
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
    }
}