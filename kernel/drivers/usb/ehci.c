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

static volatile uint8_t* ehci_mmio_base = 0;
static volatile uint8_t* ehci_op_base = 0;

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

    // 3. Kontrolcüyü yeniden çalıştır (Run/Stop = 1)
    usbcmd = *((volatile uint32_t*)(ehci_op_base + EHCI_USBCMD));
    usbcmd |= 1; // RS bitini set et
    *((volatile uint32_t*)(ehci_op_base + EHCI_USBCMD)) = usbcmd;

    serial_write("EHCI: Controller started and running.\n");
    return true;
}