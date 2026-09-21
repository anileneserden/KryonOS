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