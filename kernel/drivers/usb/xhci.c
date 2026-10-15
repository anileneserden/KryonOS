#include <kernel/drivers/usb/xhci.h>
#include <kernel/drivers/pci.h>
#include <kernel/serial.h>
#include <kernel/mem/heap.h>
#include <stdint.h>
#include <stddef.h>

#define XHCI_CLASS_CODE 0x0C
#define XHCI_SUBCLASS   0x03
#define XHCI_PROG_IF    0x30
#define XHCI_EVENT_RING_TRBS 16

typedef struct {
    volatile uint8_t caplength;
    volatile uint8_t reserved;
    volatile uint16_t hciversion;
    volatile uint32_t hcsparams1;
    volatile uint32_t hcsparams2;
    volatile uint32_t hcsparams3;
    volatile uint32_t hccparams1;
    volatile uint32_t dboff;
    volatile uint32_t rtsoff;
    volatile uint32_t hccparams2;
} __attribute__((packed)) xhci_caps_t;

typedef struct {
    volatile uint32_t usbcmd;
    volatile uint32_t usbsts;
    volatile uint32_t pagesize;
    volatile uint8_t reserved1[20];
    volatile uint32_t dnctrl;
    volatile uint8_t reserved2[20];
    volatile uint64_t crcr;
    volatile uint8_t reserved3[16];
    volatile uint64_t dcbaap;
    volatile uint32_t config;
} __attribute__((packed)) xhci_op_regs_t;

_Static_assert(offsetof(xhci_op_regs_t, dnctrl) == 0x20, "xHCI DNCTRL offset");
_Static_assert(offsetof(xhci_op_regs_t, crcr) == 0x38, "xHCI CRCR offset");
_Static_assert(offsetof(xhci_op_regs_t, dcbaap) == 0x50, "xHCI DCBAAP offset");
_Static_assert(offsetof(xhci_op_regs_t, config) == 0x58, "xHCI CONFIG offset");

static volatile xhci_caps_t *xhci_caps;
static volatile xhci_op_regs_t *xhci_op;
static uint64_t xhci_mmio_base;
static void *global_event_ring;
static uint32_t event_ring_index;
static uint8_t event_ring_cs = 1;

static void *xhci_alloc_aligned(size_t size, uintptr_t alignment) {
    void *allocation = kzalloc(size + alignment - 1);
    if (!allocation) return NULL;

    uintptr_t address = (uintptr_t)allocation;
    address = (address + alignment - 1) & ~(alignment - 1);
    return (void *)address;
}

int xhci_detect_controller(void) {
    return pci_get_device_by_class(XHCI_CLASS_CODE, XHCI_SUBCLASS, XHCI_PROG_IF) != NULL;
}

void xhci_init(void) {
    serial_write("[xHCI] Initializing xHCI driver...\n");

    pci_device_t *pci_dev = pci_get_device_by_class(XHCI_CLASS_CODE, XHCI_SUBCLASS, XHCI_PROG_IF);
    if (!pci_dev) {
        serial_write("[xHCI] ERROR: xHCI controller not found on PCI bus!\n");
        return;
    }

    pci_enable_bus_mastering(pci_dev);

    uint32_t bar0 = pci_dev->bar[0];
    if (bar0 == 0 || (bar0 & 1)) {
        serial_write("[xHCI] ERROR: Invalid xHCI MMIO BAR0.\n");
        return;
    }

    uint64_t bar_phys = bar0 & 0xFFFFFFF0;
    if ((bar0 & 0x6) == 0x4) {
        bar_phys |= (uint64_t)pci_dev->bar[1] << 32;
    }
    if (bar_phys > 0xFFFFFFFF) {
        serial_write("[xHCI] ERROR: xHCI MMIO BAR is above the 32-bit address space.\n");
        return;
    }

    xhci_mmio_base = bar_phys;
    xhci_caps = (volatile xhci_caps_t *)(uintptr_t)xhci_mmio_base;
    if (xhci_caps->caplength < sizeof(xhci_caps_t)) {
        serial_write("[xHCI] ERROR: Invalid capability register length.\n");
        return;
    }
    xhci_op = (volatile xhci_op_regs_t *)(uintptr_t)(xhci_mmio_base + xhci_caps->caplength);

    xhci_op->usbcmd &= ~1U;
    uint32_t timeout = 1000000;
    while (!(xhci_op->usbsts & 1) && --timeout) {}
    if (!timeout) {
        serial_write("[xHCI] ERROR: Controller did not halt.\n");
        return;
    }

    xhci_op->usbcmd |= 1U << 1;
    timeout = 1000000;
    while ((xhci_op->usbcmd & (1U << 1)) && --timeout) {}
    while ((xhci_op->usbsts & (1U << 11)) && --timeout) {}
    if (!timeout) {
        serial_write("[xHCI] ERROR: Controller reset timed out.\n");
        return;
    }

    uint8_t max_slots = xhci_caps->hcsparams1 & 0xFF;
    if (!max_slots) {
        serial_write("[xHCI] ERROR: Controller reports zero device slots.\n");
        return;
    }
    xhci_op->config = (xhci_op->config & ~0xFFU) | max_slots;

    uint64_t *dcbaa = (uint64_t *)xhci_alloc_aligned((max_slots + 1U) * sizeof(uint64_t), 64);
    uint32_t *command_ring = (uint32_t *)xhci_alloc_aligned(16 * 16, 64);
    uint32_t *event_ring = (uint32_t *)xhci_alloc_aligned(XHCI_EVENT_RING_TRBS * 16, 64);
    uint32_t *erst = (uint32_t *)xhci_alloc_aligned(16, 64);
    if (!dcbaa || !command_ring || !event_ring || !erst) {
        serial_write("[xHCI] ERROR: Could not allocate aligned controller structures.\n");
        return;
    }

    command_ring[15 * 4] = (uint32_t)(uintptr_t)command_ring;
    command_ring[15 * 4 + 1] = 0;
    command_ring[15 * 4 + 2] = 0;
    command_ring[15 * 4 + 3] = (6U << 10) | (1U << 1) | 1U;
    xhci_op->dcbaap = (uint64_t)(uintptr_t)dcbaa;
    xhci_op->crcr = (uint64_t)(uintptr_t)command_ring | 1U;

    erst[0] = (uint32_t)(uintptr_t)event_ring;
    erst[1] = 0;
    erst[2] = XHCI_EVENT_RING_TRBS;
    erst[3] = 0;
    global_event_ring = event_ring;
    event_ring_index = 0;
    event_ring_cs = 1;

    uint32_t rtsoff = xhci_caps->rtsoff & ~0x1FU;
    volatile uint32_t *runtime_regs = (volatile uint32_t *)(uintptr_t)(xhci_mmio_base + rtsoff);
    runtime_regs[0x28 / 4] = 1;
    runtime_regs[0x30 / 4] = (uint32_t)(uintptr_t)erst;
    runtime_regs[0x34 / 4] = 0;
    runtime_regs[0x38 / 4] = (uint32_t)(uintptr_t)event_ring;
    runtime_regs[0x3C / 4] = 0;

    xhci_op->usbcmd |= 1U;
    timeout = 1000000;
    while ((xhci_op->usbsts & 1) && --timeout) {}
    if (!timeout) {
        serial_write("[xHCI] ERROR: Controller failed to enter Run state.\n");
        global_event_ring = NULL;
        return;
    }

    uint8_t max_ports = (xhci_caps->hcsparams1 >> 24) & 0xFF;
    volatile uint32_t *portsc = (volatile uint32_t *)(uintptr_t)((uintptr_t)xhci_op + 0x400);
    for (uint32_t port = 0; port < max_ports; port++) {
        if (portsc[port * 4] & 1) {
            serial_write("[xHCI] Device connected; USB enumeration is not implemented yet.\n");
        }
    }

    serial_write("[xHCI] Controller running; event ring polling enabled.\n");
}

uint8_t xhci_poll(void) {
    if (!global_event_ring) return 0;

    volatile uint32_t *trb = (volatile uint32_t *)((uintptr_t)global_event_ring + event_ring_index * 16);
    if ((trb[3] & 1) != event_ring_cs) return 0;

    event_ring_index++;
    if (event_ring_index == XHCI_EVENT_RING_TRBS) {
        event_ring_index = 0;
        event_ring_cs ^= 1;
    }

    uint32_t rtsoff = xhci_caps->rtsoff & ~0x1FU;
    volatile uint32_t *runtime_regs = (volatile uint32_t *)(uintptr_t)(xhci_mmio_base + rtsoff);
    uint64_t dequeue = (uint64_t)(uintptr_t)global_event_ring + event_ring_index * 16;
    runtime_regs[0x38 / 4] = (uint32_t)dequeue | (1U << 3);
    runtime_regs[0x3C / 4] = (uint32_t)(dequeue >> 32);

    return 1;
}