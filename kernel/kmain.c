#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <kernel/serial.h>
#include <kernel/multiboot.h>
#include <kernel/drivers/video/fb.h>
#include <kernel/drivers/storage/ata.h>

void kernel_main(uint32_t mboot_magic, uint32_t* mboot_info_addr) {
    serial_init();
    serial_write("KryonOS baslatildi!\n");

    if (mboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        serial_write("HATA: Gecersiz magic number!\n");
        while (1) { __asm__ volatile("hlt"); }
    }

    multiboot_info_t* mboot = (multiboot_info_t*) mboot_info_addr;

    // Framebuffer ve ATA sürücüsünü başlat
    fb_init(mboot);
    ata_init();

    uint32_t width = fb_get_width();
    uint32_t height = fb_get_height();

    if (width > 0 && height > 0) {
        fb_clear(0xFF0000FF); // Mavi ekran
        fb_draw_rect(0, 0, 100, 100, 0xFFFFFFFF); // Beyaz kare
    }

    // 1. Yazılacak veriyi hazırla (512 baytlık bir sektör)
    uint8_t write_buf[512];
    for (int i = 0; i < 512; i++) {
        write_buf[i] = 0; // Önce tamamını sıfırla
    }

    const char* test_msg = "KryonOS KRYFS disk test basarili!";
    for (int i = 0; test_msg[i] != '\0'; i++) {
        write_buf[i] = (uint8_t)test_msg[i];
    }

    // 2. Diskin 1. sektörüne yaz
    serial_write("Diske veri yaziliyor...\n");
    ata_write_sector(1, write_buf);

    // 3. Okuma için boş bir tampon oluştur ve diskten oku
    uint8_t read_buf[512];
    for (int i = 0; i < 512; i++) {
        read_buf[i] = 0;
    }

    serial_write("Diskten veri okunuyor...\n");
    ata_read_sector(1, read_buf);

    // 4. Okunan veriyi seri porta yazdır
    serial_write("Diskten okunan icerik: ");
    serial_write((char*)read_buf);
    serial_write("\n");

    while (1) {
        __asm__ volatile ("hlt");
    }
}