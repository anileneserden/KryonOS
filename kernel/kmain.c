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

// Seri port üzerinden sayıları basabilmek için basit yardımcı fonksiyon
static void serial_write_dec(uint32_t val) {
    if (val == 0) {
        serial_write("0");
        return;
    }
    char buf[12];
    int i = 10;
    buf[11] = '\0';
    while (val > 0 && i >= 0) {
        buf[i--] = '0' + (val % 10);
        val /= 10;
    }
    serial_write(&buf[i + 1]);
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
    heap_init(0x600000, 0x1000000);

    // 2. PCI ve Ekran Donanım Sürücüleri
    pci_init();
    fb_init(mboot);
    ata_init();

    // 3. Dosya Sistemleri ve Girdi Sürücüleri
    vfs_init();
    kryos_fs_system_init();

    if (fat32_init_disk(1, 0)) {
        fs_driver_t fat32_driver = fat32_get_driver();
        vfs_mount('D', "FAT32_VOL", fat32_driver);
        vfs_list_drive('D');
    } else {
        serial_write("FAT32: Surucu baslatilamadi!\n");
    }

    test_fat32_read();

    mouse_init(); 
    keyboard_init();

    // 4. Grafik Arayüzünün Başlatılması ve İlk Çizim
    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    if (width > 0 && height > 0) {
        fb_clear(0xFF0000FF); 
        desktop_init(); 
    }

    app_manager_init();
    if (!kef_load_and_run("C:/Kryon/System32/mediaplayer.kef")) {
        kef_load_and_run("C:/mediaplayer.kef");
    }
    // app_create("Not Defteri", 250, 180, 0, sample_app_draw);

    fb_swap();

    // 5. AC97 Ses Sürücüsü ve WAV Oynatıcı
    if (ac97_init() == 0) {
        ac97_set_master_volume(100);
        wav_play_file("C:/Kryon/Media/startup.wav");
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