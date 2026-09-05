#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void kernel_main(void) {
    // VGA metin tamponu (text buffer) adresi: 0xB8000
    volatile uint16_t* vga_buffer = (uint16_t*) 0xB8000;
    
    const char* str = "KryonOS e Hos Geldiniz!";
    
    for (size_t i = 0; str[i] != '\0'; i++) {
        // Beyaz renk üzerine gri/mavi karakter yazımı (0x0F = Beyaz)
        vga_buffer[i] = (uint16_t) str[i] | (uint16_t) 0x0F00;
    }

    while (1) {
        __asm__ volatile ("hlt");
    }
}