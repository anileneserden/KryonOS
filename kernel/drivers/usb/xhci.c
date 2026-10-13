#include <kernel/drivers/usb/xhci.h>
#include <kernel/drivers/pci.h>
#include <kernel/serial.h>
#include <kernel/mem/vmm.h>
#include <stdint.h>
#include <stddef.h>

// xHCI Sınıf Kodları (PCI)
#define XHCI_CLASS_CODE    0x0C
#define XHCI_SUBCLASS      0x03
#define XHCI_PROG_IF       0x30

// xHCI Register Yapıları
typedef struct {
    volatile uint8_t  caplength;
    volatile uint8_t  reserved;
    volatile uint16_t hciversion;
    volatile uint32_t hccparams1;
    volatile uint32_t dboff;
    volatile uint32_t rtsoff;
    volatile uint32_t hccparams2;
} __attribute__((packed)) xhci_caps_t;

typedef struct {
    volatile uint32_t usbcmd;
    volatile uint32_t usbsts;
    volatile uint32_t pagesize;
    volatile uint8_t  reserved1[20];
    volatile uint32_t dnctrl;
    volatile uint64_t crcr;
    volatile uint8_t  reserved2[16];
    volatile uint64_t dcbaap;
    volatile uint32_t config;
} __attribute__((packed)) xhci_op_regs_t;

static volatile xhci_caps_t *xhci_caps = NULL;
static volatile xhci_op_regs_t *xhci_op = NULL;
static uint64_t xhci_mmio_base = 0;

void xhci_init(void) {
    serial_write("[xHCI] xHCI surucusu baslatiliyor...\n");

    // 1. PCI uzerinden xHCI kontrolcusunu bul (Class: 0x0C, Subclass: 0x03, Prog IF: 0x30)
    pci_device_t *pci_dev = pci_get_device_by_class(XHCI_CLASS_CODE, XHCI_SUBCLASS, XHCI_PROG_IF);
    if (!pci_dev) {
        serial_write("[xHCI] HATA: PCI uzerinde xHCI kontrolcusu bulunamadi!\n");
        return;
    }

    serial_write("[xHCI] xHCI kontrolcusu bulundu (PCI Bus: ");
    // Basit bir loglama veya adres yazdırma
    
    // 2. Bus Mastering ve Bellek Erişimini Etkinleştir
    pci_enable_bus_mastering(pci_dev);

    // 3. BAR0 Adresini Al (Genellikle xHCI MMIO tabanı BAR0'dadır)
    uint32_t bar0 = pci_dev->bar[0];
    if (bar0 == 0) {
        serial_write("[xHCI] HATA: xHCI BAR0 adresi gecersiz!\n");
        return;
    }

    // Fiziksel adresi temizle (alt bitler bayraklar için kullanılır)
    uint32_t bar_phys = bar0 & 0xFFFFFFF0;
    
    // Not: vmm_map fonksiyonun varsa burada sanal belleğe map'lemen gerekir. 
    // Örnek olarak doğrudan fiziksel adresi kullanıyoruz veya vmm_map çağrısı ekleyebilirsin:
    // vmm_map(bar_phys, bar_phys, VMM_FLAG_WRITE | VMM_FLAG_PRESENT);
    xhci_mmio_base = (uint64_t)bar_phys;

    xhci_caps = (volatile xhci_caps_t *)xhci_mmio_base;
    uint8_t caplength = xhci_caps->caplength;
    uint16_t version = xhci_caps->hciversion;

    serial_write("[xHCI] xHCI surumu basariyla okundu.\n");

    // Operational Registers = MMIO Base + CAPLENGTH
    xhci_op = (volatile xhci_op_regs_t *)(xhci_mmio_base + caplength);

    // 4. Denetleyiciyi Durdur (Run/Stop = 0)
    if (!(xhci_op->usbsts & (1 << 0))) { // HCHalted degilse durdur
        xhci_op->usbcmd &= ~1; // RS = 0
        // Halted olmasini bekle
        int timeout = 1000;
        while (!(xhci_op->usbsts & (1 << 0)) && --timeout > 0);
    }

    // 5. Denetleyiciyi Sıfırla (HCRST)
    xhci_op->usbcmd |= (1 << 1); // HCRST = 1
    int timeout = 1000;
    while ((xhci_op->usbcmd & (1 << 1)) && --timeout > 0);

    if (timeout == 0) {
        serial_write("[xHCI] HATA: xHCI sifirlama zaman asimina ugradi!\n");
        return;
    }

    serial_write("[xHCI] xHCI basariyla sifirlandi ve yapilandirmaya hazir!\n");
}