#include <kernel/drivers/audio/ac97.h>
#include <kernel/drivers/pci.h>
#include <kernel/mem/heap.h>
#include <kernel/serial.h>

// Linker hatasını önlemek için garanti Port I/O fonksiyonları
static inline void ac97_outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t ac97_inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
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

int ac97_init(void) {
    serial_write("AC97: Surucu baslatiliyor...\n");

    // 1. PCI altyapısından AC97 kartını iste
    pci_device_t* dev = pci_get_device(AC97_VENDOR_ID, AC97_DEVICE_ID);
    if (!dev) {
        serial_write("AC97: PCI uzerinde AC97 cihazı bulunamadi!\n");
        return -1;
    }

    // 2. Bus Master ve I/O Yetkilerini Aktif Et
    pci_enable_bus_mastering(dev);

    // 3. BAR Adreslerini Al
    ac97_dev.nambar  = dev->bar[0] & 0xFFFE;
    ac97_dev.nabmbar = dev->bar[1] & 0xFFFE;
    ac97_dev.irq     = dev->irq;
    ac97_dev.found   = 1;

    // 4. Cold Reset (Donanımsal Sıfırlama)
    ac97_outl(ac97_dev.nabmbar + AC97_GLOB_CNT, 0x00000002);
    for (volatile int i = 0; i < 10000; i++);
    ac97_outl(ac97_dev.nabmbar + AC97_GLOB_CNT, 0x00000000);

    // 5. Ses Seviyelerini Maksimuma Getir
    ac97_outw(ac97_dev.nambar + AC97_MASTER_VOL, 0x0000);
    ac97_outw(ac97_dev.nambar + AC97_PCM_OUT_VOL, 0x0000);

    // 6. DMA BDL Listesi İçin Bellek Ayır
    bdl_list = (ac97_bdl_entry_t*)kmalloc(sizeof(ac97_bdl_entry_t) * 32);

    serial_write("AC97: Surucu basariyla kuruldu ve hazir.\n");
    return 0;
}

void ac97_set_master_volume(uint8_t volume) {
    if (!ac97_dev.found) return;
    uint8_t vol = 31 - (volume & 31);
    uint16_t val = (vol << 8) | vol;
    ac97_outw(ac97_dev.nambar + AC97_MASTER_VOL, val);
}

void ac97_play_sound(uint16_t* buffer, uint32_t length) {
    if (!ac97_dev.found || !bdl_list) return;

    // DMA Tamponunu Hazırla
    bdl_list[0].ptr = (uint32_t)buffer;
    bdl_list[0].samples = (length / 2);
    bdl_list[0].flags = 0x8000; // Interrupt on Completion (IOC)

    // BDL Adresini Donanıma Bildir
    ac97_outl(ac97_dev.nabmbar + AC97_PO_BDBAR, (uint32_t)bdl_list);

    // Last Valid Index = 0
    ac97_outb(ac97_dev.nabmbar + AC97_PO_LVI, 0);

    // Çalmayı Başlat (Run Bitini Set Et)
    uint8_t cr = ac97_inb(ac97_dev.nabmbar + AC97_PO_CR);
    ac97_outb(ac97_dev.nabmbar + AC97_PO_CR, cr | AC97_CR_RPBM);

    serial_write("AC97: Ses calmaya basladi.\n");
}