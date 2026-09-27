#include "kernel/drivers/audio/pcspeaker.h"
#include "arch/x86/io.h"

#define PIT_CHANNEL_2_PORT 0x42
#define PIT_COMMAND_PORT   0x43
#define PPI_SYSTEM_PORT    0x61
#define PIT_BASE_FREQ      1193180

void pcspeaker_play(uint32_t freq) {
    if (freq == 0) return;

    uint32_t div = PIT_BASE_FREQ / freq;

    outb(PIT_COMMAND_PORT, 0xB6);
    outb(PIT_CHANNEL_2_PORT, (uint8_t)(div & 0xFF));
    outb(PIT_CHANNEL_2_PORT, (uint8_t)((div >> 8) & 0xFF));

    uint8_t current = inb(PPI_SYSTEM_PORT);
    if (current != (current | 3)) {
        outb(PPI_SYSTEM_PORT, current | 3);
    }
}

void pcspeaker_stop(void) {
    uint8_t current = inb(PPI_SYSTEM_PORT) & 0xFC;
    outb(PPI_SYSTEM_PORT, current);
}

void pcspeaker_beep(uint32_t freq, uint32_t duration_ms) {
    pcspeaker_play(freq);
    
    // Geçici bekleme döngüsü (Eğer RTC/PIT timer delay fonksiyonun varsa onu çağırabilirsin)
    for (volatile uint32_t i = 0; i < duration_ms * 10000; i++);

    pcspeaker_stop();
}