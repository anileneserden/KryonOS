#include <kernel/installer.h>
#include <kernel/serial.h>
#include <kernel/drivers/video/fb.h>

void installer_main(void) {
    serial_write("[INSTALLER] KryonOS Installer started via --install flag.\n");

    // Ekranı kurulum için şık koyu bir renk ile temizle (Örn: Koyu Gri / Antrasit)
    fb_clear(0xFF1E1E1E);

    // Eğer sistem çift tampon (double buffering) kullanıyorsa ekrana yansıtmak için swap gerekebilir:
    fb_swap();

    while (1) {
        // Kurulum sihirbazı ana döngüsü
        __asm__ volatile("hlt");
    }
}