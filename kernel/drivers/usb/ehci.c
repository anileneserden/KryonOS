#include <kernel/drivers/usb/ehci.h>
#include <kernel/drivers/pci.h>
#include <kernel/serial.h>
#include <kernel/mem/heap.h>
#include <arch/x86/io.h>

// Capability Registers Offsetleri
#define EHCI_CAPLENGTH   0x00
#define EHCI_HCSPARAMS   0x04
#define EHCI_HCCPARAMS   0x08

// Operational Registers Offsetleri (Base + CAPLENGTH üzerinden hesaplanır)
#define EHCI_USBCMD      0x00
#define EHCI_USBSTS      0x04
#define EHCI_USBINTR     0x08
#define EHCI_FRINDEX     0x0C
#define EHCI_CTRLDSSEG   0x10
#define EHCI_PERIODICLIST 0x14
#define EHCI_ASYNCLISTADDR 0x18
#define EHCI_CONFIGFLAG  0x40
#define EHCI_PORTSC      0x64

static volatile uint8_t* ehci_mmio_base = 0;
static volatile uint8_t* ehci_op_base = 0;

// EHCI Queue Head (QH) yapısı (32-bit hizalı olmalı)
typedef struct {
    uint32_t horizontal_link;
    uint32_t endpoint_chars;
    uint32_t endpoint_caps;
    uint32_t current_dtd;
    uint32_t next_dtd;
    uint32_t alt_next_dtd;
    uint32_t token;
    uint32_t buffer_pointers[5];
} __attribute__((packed)) ehci_qh_t;

static ehci_qh_t* async_qh = 0;

void ehci_check_ports(void) {
    serial_write("EHCI: Checking port statuses and powering ports...\n");

    uint32_t hcsparams = *((volatile uint32_t*)(ehci_mmio_base + EHCI_HCSPARAMS));
    uint8_t n_ports = hcsparams & 0x0F;
    
    serial_write("EHCI: Total downstream ports found: ");
    serial_write_dec(n_ports);
    serial_write("\n");

    // Portlara güç ver (Bit 12) ve yönlendirmeyi kesinleştir
    for (uint8_t i = 0; i < n_ports; i++) {
        volatile uint32_t* portsc = (volatile uint32_t*)(ehci_op_base + EHCI_PORTSC + (i * 4));
        uint32_t val = *portsc;
        *portsc = val | (1 << 12); // Port Power On
    }

    // Kısa bir bekleme ve durum okuma döngüsü
    for (volatile int attempt = 0; attempt < 3; attempt++) {
        for (uint8_t i = 0; i < n_ports; i++) {
            volatile uint32_t* portsc = (volatile uint32_t*)(ehci_op_base + EHCI_PORTSC + (i * 4));
            uint32_t val = *portsc;

            if (val & (1 << 0)) { // Connect Status Change / Connected
                serial_write("EHCI Port ");
                serial_write_dec(i + 1);
                serial_write(" -> Device Connected! Status Raw: 0x");
                serial_write_hex8((val >> 24) & 0xFF);
                serial_write_hex8((val >> 16) & 0xFF);
                serial_write_hex8((val >> 8) & 0xFF);
                serial_write_hex8(val & 0xFF);
                serial_write("\n");
                
                // Portu resetle (Bit 8)
                *portsc = val | (1 << 8);
            }
        }
    }
}

// Asenkron Kuyruk (Async Schedule) Altyapısını Başlatma
static void ehci_init_async_schedule(void) {
    serial_write("EHCI: Initializing Asynchronous Schedule...\n");

    // Heap üzerinden Queue Head için bellek tahsis et (32 bayt hizalama önemli)
    async_qh = (ehci_qh_t*)kmalloc(sizeof(ehci_qh_t));
    if (!async_qh) {
        serial_write("EHCI Error: Failed to allocate memory for Async QH!\n");
        return;
    }

    // QH yapısını sıfırla
    uint8_t* ptr = (uint8_t*)async_qh;
    for (uint32_t i = 0; i < sizeof(ehci_qh_t); i++) {
        ptr[i] = 0;
    }

    // QH işaretçisini kendi üzerine bağla ve Terminator (Bit 0 = 1) yap
    uint32_t qh_phys = (uint32_t)async_qh;
    async_qh->horizontal_link = qh_phys | (1 << 1); // Tip 1: Queue Head
    async_qh->horizontal_link |= 1; // Terminate = 1 (başka öğe yok)

    // Endpoint Characteristics (Device Address = 0, Endpt = 0, Maximum Packet Length vb.)
    async_qh->endpoint_chars = (1 << 15) | (64 << 16); // Head of reclamation list (Bit 15) + Max Packet Size (64)
    async_qh->token = (1 << 7); // Inactive

    // ASYNCLISTADDR register'ına QH adresini yaz
    *((volatile uint32_t*)(ehci_op_base + EHCI_ASYNCLISTADDR)) = qh_phys;

    // USBCMD içerisinden Asynchronous Schedule Enable (Bit 5) bitini set et
    uint32_t usbcmd = *((volatile uint32_t*)(ehci_op_base + EHCI_USBCMD));
    usbcmd |= (1 << 5); // Asynchronous Schedule Enable
    *((volatile uint32_t*)(ehci_op_base + EHCI_USBCMD)) = usbcmd;

    serial_write("EHCI: Asynchronous Schedule enabled successfully.\n");
}

bool ehci_init(void) {
    serial_write("EHCI: Initializing USB 2.0 driver...\n");

    pci_device_t* dev = pci_get_device_by_class(0x0C, 0x03, 0x20);
    if (!dev) {
        serial_write("EHCI: No EHCI controller found on PCI bus.\n");
        return false;
    }

    // Bus mastering yetkisini aç
    pci_enable_bus_mastering(dev);

    uint32_t bar0 = dev->bar[0] & ~0xF;
    if (bar0 == 0) {
        serial_write("EHCI Error: Invalid BAR0 address!\n");
        return false;
    }

    ehci_mmio_base = (volatile uint8_t*)bar0;
    
    // Capability length offsetini alarak Operational Registers başlangıcını bul
    uint8_t cap_len = *((volatile uint8_t*)(ehci_mmio_base + EHCI_CAPLENGTH));
    ehci_op_base = ehci_mmio_base + cap_len;

    serial_write("EHCI: Controller found. MMIO Base mapped successfully.\n");

    // 1. Kontrolcüyü durdur (Run/Stop = 0)
    uint32_t usbcmd = *((volatile uint32_t*)(ehci_op_base + EHCI_USBCMD));
    usbcmd &= ~1; // RS bitini temizle
    *((volatile uint32_t*)(ehci_op_base + EHCI_USBCMD)) = usbcmd;

    // Durmasını bekle (HCHalted bitinin 1 olmasını bekle)
    uint32_t timeout = 100000;
    while (!((*((volatile uint32_t*)(ehci_op_base + EHCI_USBSTS)) & (1 << 12))) && --timeout);

    // 2. Route Port to EHCI (Config Flag = 1)
    *((volatile uint32_t*)(ehci_op_base + EHCI_CONFIGFLAG)) = 1;

    // Asenkron kuyruk altyapısını kur
    ehci_init_async_schedule();

    // 3. Kontrolcüyü yeniden çalıştır (Run/Stop = 1)
    usbcmd = *((volatile uint32_t*)(ehci_op_base + EHCI_USBCMD));
    usbcmd |= 1; // RS bitini set et
    *((volatile uint32_t*)(ehci_op_base + EHCI_USBCMD)) = usbcmd;

    serial_write("EHCI: Controller started and running.\n");

    // 4. Port durumlarını kontrol et ve cihazları tara
    ehci_check_ports();

    return true;
}