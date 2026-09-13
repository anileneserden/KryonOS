#include <kernel/drivers/audio/ac97.h>
#include <kernel/drivers/pci.h>
#include <kernel/mem/heap.h>
#include <kernel/serial.h>

static inline void ac97_outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t ac97_inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline uint16_t ac97_inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void ac97_outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void ac97_outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static ac97_device_t ac97_dev;
static ac97_bdl_entry_t* bdl_list = 0;
static int16_t* pcm_buffer = 0;

int ac97_init(void) {
    serial_write("AC97: Surucu baslatiliyor...\n");

    pci_device_t* dev = pci_get_device(AC97_VENDOR_ID, AC97_DEVICE_ID);
    if (!dev) {
        serial_write("AC97: PCI uzerinde AC97 cihazı bulunamadi!\n");
        return -1;
    }

    pci_enable_bus_mastering(dev);

    ac97_dev.nambar  = dev->bar[0] & 0xFFFE;
    ac97_dev.nabmbar = dev->bar[1] & 0xFFFE;
    ac97_dev.irq     = dev->irq;
    ac97_dev.found   = 1;

    // Cold Reset
    ac97_outl(ac97_dev.nabmbar + AC97_GLOB_CNT, 0x00000002);
    for (volatile int i = 0; i < 10000; i++);
    ac97_outl(ac97_dev.nabmbar + AC97_GLOB_CNT, 0x00000000);

    // Ses Seviyelerini Aç
    ac97_outw(ac97_dev.nambar + AC97_MASTER_VOL, 0x0000);
    ac97_outw(ac97_dev.nambar + AC97_PCM_OUT_VOL, 0x0000);

    // Tampon Ayırma
    bdl_list = (ac97_bdl_entry_t*)kmalloc(sizeof(ac97_bdl_entry_t) * 32);
    pcm_buffer = (int16_t*)kmalloc(48000 * sizeof(int16_t) * 2);

    serial_write("AC97: Surucu basariyla kuruldu ve hazir.\n");
    return 0;
}

void ac97_set_master_volume(uint8_t volume) {
    if (!ac97_dev.found) return;
    uint8_t vol = 31 - (volume & 31);
    uint16_t val = (vol << 8) | vol;
    ac97_outw(ac97_dev.nambar + AC97_MASTER_VOL, val);
}

void ac97_set_sample_rate(uint32_t hz) {
    if (!ac97_dev.found) return;
    ac97_outw(ac97_dev.nambar + AC97_PCM_FRONT_DAC_RATE, (uint16_t)hz);
}

void ac97_play_sound(uint16_t* buffer, uint32_t length) {
    if (!ac97_dev.found || !bdl_list || !buffer || length == 0) return;

    // AC'97 DMA Kontrolcüsünü durdur
    ac97_outb(ac97_dev.nabmbar + AC97_PO_CR, 0x00);

    // 16-bit stereo/mono için toplam sample (word) sayısı
    uint32_t total_samples = length / 2;
    uint32_t remaining_samples = total_samples;
    uint32_t current_offset = 0;
    int bdl_index = 0;

    // BDL tablosunu temizle
    memset(bdl_list, 0, sizeof(ac97_bdl_entry_t) * 32);

    // Veriyi 32768 sample'lık (64 KB) parçalara bölerek BDL tablosuna doldur
    while (remaining_samples > 0 && bdl_index < 32) {
        uint32_t chunk_samples = (remaining_samples > 32768) ? 32768 : remaining_samples;

        // VMM mimarinde identity-mapping kullanıldığı için buffer adresi doğrudan fiziksel adrestir.
        bdl_list[bdl_index].ptr = (uint32_t)(buffer + current_offset);
        bdl_list[bdl_index].samples = (uint16_t)chunk_samples;
        
        // Varsayılan bayrak 0 (Devam ediyor)
        bdl_list[bdl_index].flags = 0;

        remaining_samples -= chunk_samples;
        current_offset += chunk_samples;
        bdl_index++;
    }

    if (bdl_index == 0) return;

    // Son BDL elemanına Interrupt On Completion (IOC) bayrağını koy
    bdl_list[bdl_index - 1].flags = 0x8000;

    // BDL Adresini yaz
    ac97_outl(ac97_dev.nabmbar + AC97_PO_BDBAR, (uint32_t)bdl_list);

    // Son geçerli indeks değerini (LVI) bildir (0-indexed olduğu için bdl_index - 1)
    ac97_outb(ac97_dev.nabmbar + AC97_PO_LVI, (uint8_t)(bdl_index - 1));

    // Transferi başlat (Run/Pause bitini set et)
    ac97_outb(ac97_dev.nabmbar + AC97_PO_CR, AC97_CR_RPBM);

    serial_write("AC97: Ses calmaya basladi (BDL parca sayisi: ");
    // basit bir log bildirimi
    serial_write("OK)\n");
}

void ac97_play_tone(uint32_t frequency, uint32_t duration_ms) {
    if (!ac97_dev.found || !bdl_list || !pcm_buffer) return;

    if (frequency == 0) {
        for (volatile uint32_t i = 0; i < duration_ms * 15000; i++);
        return;
    }

    uint32_t sample_rate = 44100;
    uint32_t total_samples = (sample_rate * duration_ms) / 1000;
    uint32_t period = sample_rate / frequency;

    for (uint32_t i = 0; i < total_samples; i++) {
        pcm_buffer[i] = ((i % period) < (period / 2)) ? 0x2000 : -0x2000;
    }

    ac97_outb(ac97_dev.nabmbar + AC97_PO_CR, 0x00);
    
    bdl_list[0].ptr = (uint32_t)pcm_buffer;
    bdl_list[0].samples = total_samples;
    bdl_list[0].flags = 0x8000;

    ac97_outw(ac97_dev.nabmbar + AC97_PO_SR, 0x001C);
    ac97_outl(ac97_dev.nabmbar + AC97_PO_BDBAR, (uint32_t)bdl_list);
    ac97_outb(ac97_dev.nabmbar + AC97_PO_LVI, 0);

    ac97_outb(ac97_dev.nabmbar + AC97_PO_CR, AC97_CR_RPBM);

    for (volatile uint32_t i = 0; i < duration_ms * 10000; i++) {
        __asm__ volatile ("pause");
    }

    ac97_outb(ac97_dev.nabmbar + AC97_PO_CR, 0x00);
}