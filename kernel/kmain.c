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
#include <kernel/drivers/pci.h>
#include <kernel/drivers/usb/ehci.h>
#include <kernel/drivers/usb/uhci.h>
#include <kernel/drivers/usb/xhci.h>
#include <ui/cursor.h>
#include <ui/desktop.h>
#include <ui/serial_screen.h>
#include <kernel/app.h>
#include <kernel/mem/heap.h>
#include <kernel/mem/pmm.h>
#include <kernel/mem/vmm.h>
#include <kernel/interrupts.h>
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

    if (debug_mode) {
        serial_screen_init();
        serial_screen_mirror_enable(true); // Tüm serial_write logları ekrana da akacak
        serial_screen_write("DEBUG MODE ACTIVE: Screen & Mirror initialized.\n");
    }

    // 2. PCI, display, and speaker hardware drivers
    pci_init();
    ata_init();

    e1000_init();

    // Donanımda hangi USB kontrolcüsü varsa, EHCI'yi test etmek için önceliği ona veriyoruz
    if (ehci_detect_controller()) {
        serial_write("[USB] EHCI controller detected. Starting EHCI driver...\n");
        ehci_init();
    } else if (xhci_detect_controller()) {
        serial_write("[USB] xHCI controller detected. Starting xHCI driver...\n");
        xhci_init();
    } else if (uhci_detect_controller()) {
        serial_write("[USB] UHCI controller detected. Starting UHCI driver...\n");
        uhci_init();
    } else {
        serial_write("[USB] WARNING: No supported USB controller found on PCI bus!\n");
    }

    // 3. Filesystems and VFS Initialization
    vfs_init();
    
    if (fat32_init_disk(1, 0)) {
        fs_driver_t fat32_driver = fat32_get_driver();
        vfs_mount('D', "FAT32_VOL", fat32_driver);
        serial_write("FAT32 mounted to D:\\\n");
    } else {
        serial_write("FAT32: Driver could not be started!\n");
    }

    // KRYFS Mount and VFS Integration (C Drive)
    if (kryfs_mount() == 0) {
        fs_driver_t kryfs_driver = kryfs_get_driver();
        vfs_mount('C', "KRYFS_VOL", kryfs_driver);
        serial_write("KRYFS mounted successfully and registered to VFS on C:\\.\n");
    } else {
        serial_write("KRYFS: Driver could not be started!\n");
    }

    serial_write("[input] xHCI USB input active.\n");

    // 5. Graphical Interface Initialization and Initial Drawing
    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    if (width > 0 && height > 0) {
        if (debug_mode) {
            serial_screen_write("Debug screen fully loaded.\n");
            while (1) {
                // PS/2 port polling kaldırıldı
            }
        } else {
            fb_clear(0xFF0000FF);
            desktop_init(); 
        }
    }

    app_manager_init();
    fb_swap();

    // 6. AC97 audio driver initialization
    if (ac97_init() == 0) {
        ac97_set_master_volume(100);
        serial_write("AC97: Driver initialized and ready.\n");
    }

    interrupts_init();
    serial_write("System initialized; idle loop polls USB EHCI events.\n");

    // 7. Main event loop (GUI active loop with USB polling)
    while (1) {
        uint8_t input_updated = 0;

        // EHCI poll mekanizmasını çağırıyoruz
        input_updated = ehci_poll();

        // Masaüstü ve uygulama güncellemelerini tetikle
        desktop_process_input();
        app_manager_update_all();

        cursor_update_and_redraw();
    }
}