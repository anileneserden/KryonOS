#include <kernel/power.h>
#include <arch/x86/io.h>

void system_shutdown(void) {
    // QEMU, Bochs and standard x86 ACPI/APM shutdown signals
    outw(0xB004, 0x2000); // QEMU ACPI shutdown
    outw(0x604, 0x2000);  // Alternative QEMU port
    outw(0x4004, 0x3400); // Bochs / older QEMU
    
    // Halt the processor if the hardware does not support it or fails to shut down
    while(1) {
        __asm__ volatile("cli; hlt");
    }
}

void system_reboot(void) {
    uint8_t temp = 0x02;
    
    // Klavye denetleyicisi giriş tamponu boşalana kadar bekle
    while (temp & 0x02) {
        temp = inb(0x64);
    }
    
    // 0x64 portuna reset komutu (0xFE) göndererek CPU'yu yeniden başlat
    outb(0x64, 0xFE);
    
    // Donanım anında reset atmazsa işlemciyi durdur
    while(1) {
        __asm__ volatile("cli; hlt");
    }
}