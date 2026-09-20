#include <kernel/power.h>
#include <arch/x86/io.h>

void system_shutdown(void) {
    // QEMU, Bochs ve standart x86 ACPI/APM kapatma sinyalleri
    outw(0xB004, 0x2000); // QEMU ACPI shutdown
    outw(0x604, 0x2000);  // Alternatif QEMU portu
    outw(0x4004, 0x3400); // Bochs / eski QEMU
    
    // Eğer donanım desteklemiyor veya kapanmıyorsa işlemciyi askıya al
    while(1) {
        __asm__ volatile("cli; hlt");
    }
}