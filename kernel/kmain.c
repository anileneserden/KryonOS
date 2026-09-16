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
#include <ui/cursor.h>
#include <ui/desktop.h>
#include <kernel/mem/heap.h>
#include <kernel/mem/pmm.h>
#include <kernel/mem/vmm.h>
#include <kernel/app.h>
#include <kernel/kef.h>
#include <arch/x86/io.h>

// Windows XP Açılış Melodisi Notaları (Hz, ms)
static note_t win_xp_tune[] = {
    {311, 200}, // D#4
    {466, 200}, // A#4
    {392, 200}, // G4
    {622, 350}, // D#5
    {466, 300}, // A#4
    {622, 600}  // D#5
};

static inline uint8_t inb_port(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void sample_app_draw(app_t* app) {
    (void)app; // -Wunused-parameter uyarısını önlemek için
}

void test_fat32_read(void) {
    uint32_t file_size = 0;
    
    // VFS üzerindeki basit read_file arayüzü kullanılır
    char* file_data = (char*) vfs_read_file("D:/TEST.TXT", &file_size);

    if (file_data == NULL || file_size == 0) {
        serial_write("[FAT32 TEST] Hata: D:/TEST.TXT acilamadi veya dosya bos!\n");
        return;
    }

    serial_write("\n========================================\n");
    serial_write(" D:\\TEST.TXT Icerigi (");
    serial_write_dec(file_size);
    serial_write(" byte):\n");
    serial_write("========================================\n");
    
    // Ekrana basarken taşmayı önlemek için 100 bayt ile sınırla veya tamamını yazdır
    uint32_t print_bytes = file_size > 100 ? 100 : file_size;
    for (uint32_t i = 0; i < print_bytes; i++) {
        char c_str[2] = { file_data[i], '\0' };
        serial_write(c_str);
    }
    
    serial_write("\n========================================\n\n");

    // Sürücünüzün read_file implementasyonunda kalloc/kmalloc yapılıyorsa kfree ekleyebilirsiniz.
}

void test_kryfs_read(void) {
    uint32_t file_size = 0;
    
    // VFS üzerinden KRYFS dosyasını oku
    char* file_data = (char*) vfs_read_file("C:/Users/anil/Desktop/test.txt", &file_size);

    if (file_data == NULL || file_size == 0) {
        serial_write("[KRYFS TEST] Hata: C:/Users/anil/Desktop/test.txt acilamadi veya dosya bos!\n");
        return;
    }

    serial_write("\n========================================\n");
    serial_write(" C:\\Users\\anil\\Desktop\\test.txt Icerigi (");
    serial_write_dec(file_size);
    serial_write(" byte):\n");
    serial_write("========================================\n");
    
    for (uint32_t i = 0; i < file_size; i++) {
        char c_str[2] = { file_data[i], '\0' };
        serial_write(c_str);
    }
    
    serial_write("\n========================================\n\n");
}

void kernel_main(uint32_t mboot_magic, uint32_t* mboot_info_addr) {
    serial_init();
    serial_write("KryonOS baslatildi!\n");

    if (mboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        serial_write("HATA: Gecersiz magic number!\n");
        while (1) { __asm__ volatile("hlt"); }
    }

    multiboot_info_t* mboot = (multiboot_info_t*) mboot_info_addr;

    // 1. Bellek Yönetimi
    pmm_init(mboot);
    vmm_init();
    heap_init(0x2000000, 1024 * 1024 * 16);

    // 2. PCI ve Ekran Donanım Sürücüleri
    pci_init();
    fb_init(mboot);
    ata_init();

    // 3. Dosya Sistemleri ve Girdi Sürücüleri
    vfs_init();
    kryos_fs_system_init();
    
    // 🔹 Buraya C sürücüsünü listeleme fonksiyonunu ekleyebilirsin:
    vfs_list_drive('C');

    if (fat32_init_disk(1, 0)) {
        fs_driver_t fat32_driver = fat32_get_driver();
        vfs_mount('D', "FAT32_VOL", fat32_driver);
        vfs_list_drive('D');
    } else {
        serial_write("FAT32: Surucu baslatilamadi!\n");
    }

    // test_fat32_read();

    test_kryfs_read();

    mouse_init(); 
    keyboard_init();

    // --- C:/Users/anil/Desktop/test.txt DOSYASINI OKUMA VE SERIAL'A YAZMA ---
    uint32_t file_size = 0;
    char* file_content = (char*)vfs_read_file("C:/Users/anil/Desktop/test.txt", &file_size);
    
    if (file_content && file_size > 0) {
        serial_write("\n[VFS] C:/Users/anil/Desktop/test.txt basariyla okundu:\n--- BASLANGIC ---\n");
        
        // Karakter karakter veya blok halinde serial porta yazdır
        for (uint32_t i = 0; i < file_size; i++) {
            char c[2] = { file_content[i], '\0' };
            serial_write(c);
        }
        
        serial_write("\n--- BITIS ---\n\n");
    } else {
        serial_write("[VFS HATA] C:/Users/anil/Desktop/test.txt okunamadi veya dosya bos!\n");
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
    /*if (!kef_load_and_run("C:/Kryon/System32/test1.kef")) {
        kef_load_and_run("C:/test1.kef");
    }*/
    app_create("Not Defteri", 250, 180, 0, sample_app_draw);

    fb_swap();

    // 5. AC97 Ses Sürücüsü ve WAV Oynatıcı
    if (ac97_init() == 0) {
        ac97_set_master_volume(100);

        // KRYFS diskinizdeki bir .wav dosyasını oynatmak için:
        //wav_play_file("C:/Kryon/Media/startup.wav");
    }

    // 6. Ana Olay Döngüsü
    while (1) {
        if (inb_port(0x64) & 1) {
            uint8_t status = inb_port(0x64);
            if (status & 0x20) {
                mouse_handler();
                wm_process_input();
            } else {
                keyboard_handler();
            }
            desktop_process_input(); 
            app_manager_update_all();
        }

        cursor_update_and_redraw();
    }
}