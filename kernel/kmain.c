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
#include <kernel/installer.h>
#include <kernel/string.h>

#ifndef MULTIBOOT_INFO_CMDLINE
#define MULTIBOOT_INFO_CMDLINE (1 << 2)
#endif

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

    // 2. PCI, display, and speaker hardware drivers
    pci_init();
    uhci_init();
    fb_init(mboot);
    ata_init();

    // --- COMMAND LINE ARGUMENT CHECK (--installer) ---
    if ((mboot->flags & MULTIBOOT_INFO_CMDLINE) && mboot->cmdline) {
        char* cmdline = (char*)mboot->cmdline;
        serial_write("Boot command line: ");
        serial_write(cmdline);
        serial_write("\n");

        if (strstr(cmdline, "--installer") != NULL) {
            serial_write("[INFO] --installer parameter detected. Launching installer...\n");
            installer_main();
            return;
        }
    }
    // ------------------------------------------------

    // 3. Filesystems and input drivers
    vfs_init();
    
    if (fat32_init_disk(1, 0)) {
        fs_driver_t fat32_driver = fat32_get_driver();
        vfs_mount('D', "FAT32_VOL", fat32_driver);
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

    // --- KRYFS / VFS FILE READ TEST ---
    uint32_t test_size = 0;
    void* file_data = vfs_read_file("C:/test.txt", &test_size);
    
    serial_write("---- test.txt File Content ----\n");
    if (file_data && test_size > 0) {
        serial_write((char*)file_data);
        serial_write("\n");
        kfree(file_data);
    } else {
        serial_write("[ERROR] test.txt could not be read or found!\n");
    }
    serial_write("--------------------------------\n");
    // ------------------------------------

    // --- FAT32 / VFS FILE WRITE TEST ---
    serial_write("---- FAT32 Write Test (D:/hello.txt) ----\n");
    const char* write_content = "Hello from KryonOS FAT32 Write Engine!\n";
    if (vfs_write_file("D:/hello.txt", write_content, 39)) {
        serial_write("VFS: hello.txt successfully written to FAT32!\n");
    } else {
        serial_write("[ERROR] hello.txt could not be written to FAT32!\n");
    }
    serial_write("-----------------------------------------\n");
    // -----------------------------------

    // --- VFS DIRECTORY LISTING TEST ---
    serial_write("---- VFS Directory Listing Test (C:/) ----\n");
    vfs_file_info_t root_files[16];
    int file_count = vfs_get_directory_files("C:/", root_files, 16);
    
    if (file_count > 0) {
        serial_write("Files/folders in C:/ directory:\n");
        for (int i = 0; i < file_count; i++) {
            serial_write(" - ");
            serial_write(root_files[i].name);
            if (root_files[i].is_directory) {
                serial_write(" [FOLDER]");
            } else {
                serial_write(" [FILE]");
            }
            serial_write("\n");
        }
    } else {
        serial_write("VFS Info: C:/ directory is empty or get_dir_files is not supported/returned 0.\n");
    }
    serial_write("-------------------------------------------\n");
    // ------------------------------------------

    // 4. Input drivers
    if (!uhci_mouse_active()) {
        mouse_init();
    } else {
        serial_write("USB mouse active; PS/2 mouse initialization skipped.\n");
    }
    keyboard_init();

    // 5. Graphical Interface Initialization and Initial Drawing
    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    if (width > 0 && height > 0) {
        fb_clear(0xFF0000FF); // Navy blue desktop background
        desktop_init(); 
    }

    
    serial_write("---- Starting KEF Application ----\n");
    if (kef_load_and_run("C:/Kryon/System32/altf4.kef")) {
        serial_write("KEF: Application successfully executed and terminated.\n");
    } else {
        serial_write("[ERROR] KEF application could not be started!\n");
    }
    serial_write("-------------------------------------\n");
    

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