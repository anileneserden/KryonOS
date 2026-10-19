#include <kernel/drivers/usb/ehci.h>
#include <kernel/drivers/pci.h>
#include <kernel/serial.h>
#include <kernel/mem/heap.h>
#include <stdint.h>
#include <stddef.h>

// EHCI Capability Registers (Base + 0x00)
typedef struct {
    volatile uint8_t caplength;
    volatile uint8_t reserved;
    volatile uint16_t hciversion;
    volatile uint32_t hcsparams;
    volatile uint32_t hccparams;
    volatile uint32_t portroute[2];
} __attribute__((packed)) ehci_caps_t;

// EHCI Operational Registers (Base + caplength)
typedef struct {
    volatile uint32_t usbcmd;
    volatile uint32_t usbsts;
    volatile uint32_t usbintr;
    volatile uint32_t frindex;
    volatile uint32_t periodiclistbase;
    volatile uint32_t asynclistaddr;
    volatile uint8_t reserved[64];
    volatile uint32_t configflag;
} __attribute__((packed)) ehci_op_regs_t;

static volatile ehci_caps_t *ehci_caps;
static volatile ehci_op_regs_t *ehci_op;
static uint64_t ehci_mmio_base = 0;
static uint8_t ehci_active = 0;

static void *ehci_alloc_aligned(size_t size, uintptr_t alignment) {
    void *allocation = kzalloc(size + alignment - 1);
    if (!allocation) return NULL;

    uintptr_t address = (uintptr_t)allocation;
    address = (address + alignment - 1) & ~(alignment - 1);
    return (void *)address;
}

int ehci_detect_controller(void) {
    return pci_get_device_by_class(EHCI_CLASS_CODE, EHCI_SUBCLASS, EHCI_PROG_IF) != NULL;
}

void ehci_init(void) {
    serial_write("[EHCI] Initializing EHCI driver (USB 2.0)...\n");

    pci_device_t *pci_dev = pci_get_device_by_class(EHCI_CLASS_CODE, EHCI_SUBCLASS, EHCI_PROG_IF);
    if (!pci_dev) {
        serial_write("[EHCI] ERROR: EHCI controller not found on PCI bus!\n");
        return;
    }

    pci_enable_bus_mastering(pci_dev);

    uint32_t bar0 = pci_dev->bar[0];
    if (bar0 == 0 || (bar0 & 1)) {
        serial_write("[EHCI] ERROR: Invalid EHCI MMIO BAR0.\n");
        return;
    }

    ehci_mmio_base = bar0 & 0xFFFFFFF0;
    ehci_caps = (volatile ehci_caps_t *)(uintptr_t)ehci_mmio_base;
    
    if (ehci_caps->caplength < sizeof(ehci_caps_t)) {
        serial_write("[EHCI] ERROR: Invalid capability register length.\n");
        return;
    }
    
    ehci_op = (volatile ehci_op_regs_t *)(uintptr_t)(ehci_mmio_base + ehci_caps->caplength);

    // 1. Denetleyiciyi Durdur (Halt)
    ehci_op->usbcmd &= ~1U;
    uint32_t timeout = 1000000;
    while (!(ehci_op->usbsts & (1 << 12)) && --timeout) {}
    if (!timeout) {
        serial_write("[EHCI] ERROR: Controller did not halt.\n");
        return;
    }

    // 2. Host Controller Reset (HCRESET)
    ehci_op->usbcmd |= (1 << 1);
    timeout = 1000000;
    while ((ehci_op->usbcmd & (1 << 1)) && --timeout) {}
    if (!timeout) {
        serial_write("[EHCI] ERROR: Controller reset timed out.\n");
        return;
    }

    // 3. Port Routing Sahipliği (BIOS/OS Hand-off -> CONFIGFLAG set et)
    ehci_op->configflag = 1;

    // 4. Asenkron Liste Adresi (Async List Address) Başlangıcı
    // İleride QH (Queue Head) zinciri buraya bağlanacak.
    ehci_op->asynclistaddr = 0;

    // 5. Denetleyiciyi Çalıştır (Run, Interrupt Enable)
    // Run/Stop = 1, Interrupt Threshold = 1 micro-frame
    ehci_op->usbcmd = (1U << 0) | (1U << 2) | (0x10 << 16);

    ehci_active = 1;
    serial_write("[EHCI] Controller running successfully.\n");
}

uint8_t ehci_poll(void) {
    if (!ehci_active) return 0;

    // TODO: Asenkron kuyruk ve transfer tamamlanma durumlarının kontrolü
    return 0;
}

uint8_t ehci_is_active(void) {
    return ehci_active;
}