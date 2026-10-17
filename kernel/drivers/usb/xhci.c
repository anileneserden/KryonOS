#include <kernel/drivers/usb/xhci.h>
#include <kernel/drivers/input/mouse_usb.h>
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
static uint32_t *global_command_ring;
static uint32_t event_ring_index;
static uint32_t command_ring_index = 0;
static uint8_t event_ring_cs = 1;
static uint8_t command_ring_cs = 1;
static uint8_t allocated_slot_id = 0;
static int xhci_driver_state = 0;
static uint32_t *global_mouse_transfer_ring = NULL;

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

    global_command_ring = command_ring; 
    global_event_ring = event_ring;
    event_ring_index = 0;
    event_ring_cs = 1;
    
    if (!dcbaa || !command_ring || !event_ring || !erst) {
        serial_write("[xHCI] ERROR: Could not allocate aligned controller structures.\n");
        return;
    }

    for (int i = 0; i < 16 * 4; i++) command_ring[i] = 0;

    command_ring[15 * 4] = (uint32_t)(uintptr_t)command_ring; 
    command_ring[15 * 4 + 1] = 0;
    command_ring[15 * 4 + 2] = 0;
    command_ring[15 * 4 + 3] = (6U << 10) | (1U << 1) | 1U; // Link TRB, TC=1, Cycle=1

    xhci_op->dcbaap = (uint64_t)(uintptr_t)dcbaa;
    xhci_op->crcr = (uint64_t)(uintptr_t)command_ring | 1U; 
    command_ring_cs = 1;

    // ERST Tablosu (Event Ring Segment Table) yapılandırması
    erst[0] = (uint32_t)(uintptr_t)event_ring;
    erst[1] = 0;
    erst[2] = XHCI_EVENT_RING_TRBS;
    erst[3] = 0;

    uint32_t rtsoff = xhci_caps->rtsoff & ~0x1FU;
    volatile uint32_t *runtime_regs = (volatile uint32_t *)(uintptr_t)(xhci_mmio_base + rtsoff);
    
    // Interrupter 0 Interrupt Enable (Bit 1) set ediliyor
    runtime_regs[0x20 / 4] |= (1U << 1); 
    runtime_regs[0x28 / 4] = (uint32_t)(uintptr_t)event_ring | (1U << 3); // ERDP Low + EHB
    runtime_regs[0x2C / 4] = 0;                                           // ERDP High
    runtime_regs[0x30 / 4] = (uint32_t)(uintptr_t)erst;                   // ERSTBA Low
    runtime_regs[0x34 / 4] = 0;                                           // ERSTBA High
    runtime_regs[0x38 / 4] = 1;                                           // ERST Size (1 segment)
    runtime_regs[0x3C / 4] = 0;

    xhci_op->usbcmd |= 1U;
    timeout = 1000000;
    while ((xhci_op->usbsts & 1) && --timeout) {}
    if (!timeout) {
        serial_write("[xHCI] ERROR: Controller failed to enter Run state.\n");
        global_event_ring = NULL;
        return;
    }

    serial_write("[xHCI] Controller running; event ring polling enabled.\n");

    // NOT: Port tarama ve reset işlemlerini xHCI poll döngüsünün (Port Status Change Event) 
    // doğal akışına bırakıyoruz. Böylece donanım event'leri kaçmaz.
}

void xhci_send_command(uint32_t trb0, uint32_t trb1, uint32_t trb2, uint32_t trb3_type_and_flags) {
    if (!global_command_ring) return;

    if (command_ring_index >= 15) {
        volatile uint32_t *link_trb = global_command_ring + (15 * 4);
        link_trb[0] = (uint32_t)(uintptr_t)global_command_ring;
        link_trb[1] = 0;
        link_trb[2] = 0;
        link_trb[3] = (6U << 10) | (1U << 1) | (uint32_t)command_ring_cs;

        command_ring_index = 0;
        command_ring_cs ^= 1;
    }

    volatile uint32_t *cmd_trb = global_command_ring + (command_ring_index * 4);
    cmd_trb[0] = trb0;
    cmd_trb[1] = trb1;
    cmd_trb[2] = trb2;
    cmd_trb[3] = trb3_type_and_flags | (uint32_t)command_ring_cs;

    command_ring_index++;

    // DİKKAT: Her komutta CRCR'yi yeniden yazmıyoruz! Sadece Doorbell (Host Controller = Slot 0) çalıyoruz.
    uint32_t dboff = xhci_caps->dboff & ~0x3U;
    volatile uint32_t *doorbell_regs = (volatile uint32_t *)(uintptr_t)(xhci_mmio_base + dboff);
    doorbell_regs[0] = 0; // Host Controller Command Doorbell (DB Target = 0)
    
    serial_write("[xHCI] Command TRB written to ring and Host Controller Doorbell rung.\n");
}

uint8_t xhci_poll(void) {
    if (!global_event_ring) return 0;

    uint8_t processed_any = 0;
    uint32_t rtsoff = xhci_caps->rtsoff & ~0x1FU;
    volatile uint32_t *runtime_regs = (volatile uint32_t *)(uintptr_t)(xhci_mmio_base + rtsoff);

    // --- xHCI Port Bağlantı ve Otomatik Reset Mekanizması ---
    if (xhci_driver_state == 0) {
        uint8_t max_ports = (xhci_caps->hcsparams1 >> 24) & 0xFF;
        volatile uint32_t *portsc = (volatile uint32_t *)(uintptr_t)((uintptr_t)xhci_op + 0x400);
        
        for (uint32_t port = 0; port < max_ports; port++) {
            uint32_t status = portsc[port * 4];
            
            // Eğer portta cihaz bağlıysa (CCS = Bit 0 set)
            if (status & 1) {
                serial_write("[xHCI] Device connected on port! Resetting port...\n");
                
                // Port Reset (Bit 4) ve Power On (Bit 9) set edilir. W1C bitleri korunur.
                portsc[port * 4] = (status & ~((1 << 9) | (1 << 10) | (1 << 11))) | (1 << 4) | (1 << 1);
                
                xhci_driver_state = 1; // State 1: Port resetlendi, PRC bekleniyor
                break;
            }
        }
    } else if (xhci_driver_state == 1) {
        // Port resetlendikten sonra PRC (Port Reset Complete - Bit 21) bayrağını kontrol edelim
        uint8_t max_ports = (xhci_caps->hcsparams1 >> 24) & 0xFF;
        volatile uint32_t *portsc = (volatile uint32_t *)(uintptr_t)((uintptr_t)xhci_op + 0x400);
        
        for (uint32_t port = 0; port < max_ports; port++) {
            uint32_t status = portsc[port * 4];
            
            // PRC (Bit 21) set oldu mu?
            if (status & (1 << 21)) {
                serial_write("[xHCI] Port Reset Complete (PRC active)!\n");
                
                // PRC bayrağını temizle (1 yazarak clear edilir), W1C bitlerini koru
                portsc[port * 4] = (status & ~((1 << 9) | (1 << 10) | (1 << 11))) | (1 << 21);

                // Enable Slot Komutunu Gönder (TRB Type 9)
                xhci_send_command(0, 0, 0, (9U << 10)); 
                serial_write("[xHCI] Enable Slot command sent!\n");
                
                xhci_driver_state = 2; // State 2: Enable Slot komutu gönderildi, Command Completion bekleniyor
                break;
            }
        }
    }
    // --------------------------------------------------------

    while (1) {
        volatile uint32_t *trb = (volatile uint32_t *)((uintptr_t)global_event_ring + event_ring_index * 16);
        uint32_t trb_3 = trb[3];

        // xHCI Cycle Bit denetimi: TRB'nin 0. bitinin consumer cycle state ile uyuşması şarttır!
        if ((trb_3 & 1) != event_ring_cs) {
            break; // Kontrolcü henüz bu event'i yazmadı, döngüden çık
        }

        uint32_t trb_type = (trb_3 >> 10) & 0x3F;
        if (trb_type == 0) {
            break;
        }

        if (trb_type == 32) { // Transfer Event (Fare verisi geldi)
            serial_write("[xHCI] Transfer Event (32) caught!\n");

            uint32_t comp_code = (trb[2] >> 24) & 0xFF;
            if (comp_code == 1) { // Success
                volatile uint8_t *mouse_packet = (volatile uint8_t *)(uintptr_t)trb[0];
                
                if (mouse_packet) {
                    usb_mouse_process_report((const uint8_t*)mouse_packet, 4);
                }

                // Sınırsız dinleme için Transfer Ring'e yeni TRB ekleyip Doorbell'i tekrar çal
                uint32_t *mouse_transfer_ring = (uint32_t *)global_mouse_transfer_ring;
                if (mouse_transfer_ring) {
                    uint8_t *mouse_buffer = (uint8_t *)xhci_alloc_aligned(64, 64);
                    if (mouse_buffer) {
                        mouse_transfer_ring[0] = (uint32_t)(uintptr_t)mouse_buffer;
                        mouse_transfer_ring[1] = 0;
                        mouse_transfer_ring[2] = 4;
                        mouse_transfer_ring[3] = (1U << 10) | (1U << 5) | 1U; // Normal TRB + IOC + Cycle

                        uint32_t dboff = xhci_caps->dboff & ~0x3U;
                        volatile uint32_t *doorbell_regs = (volatile uint32_t *)(uintptr_t)(xhci_mmio_base + dboff);
                        doorbell_regs[1] = 1; 
                    }
                }
            }
        } else if (trb_type == 33) { // Command Completion Event
            uint32_t comp_code = (trb[2] >> 24) & 0xFF;
            uint32_t slot_id = (trb_3 >> 24) & 0xFF;
            
            serial_write("[xHCI] Command Completion (33) caught!\n");

            if (comp_code == 1) { // Success
                if (xhci_driver_state == 2) {
                    allocated_slot_id = slot_id ? slot_id : 1;
                    serial_write("[xHCI] Enable Slot completed successfully. Sending Address Device...\n");
                    
                    // State 3: Address Device Komutu Gönder (TRB Type 11)
                    xhci_send_command(0, 0, 0, (11U << 10) | (allocated_slot_id << 24));
                    
                    xhci_driver_state = 3; 
                } else if (xhci_driver_state == 3) {
                    serial_write("[xHCI] Address Device completed successfully!\n");
                    
                    // Transfer Ring'i kur ve ilk doorbell'ı çal
                    uint32_t *mouse_transfer_ring = (uint32_t *)xhci_alloc_aligned(16 * 16, 64);
                    if (mouse_transfer_ring) {
                        global_mouse_transfer_ring = mouse_transfer_ring;
                        uint8_t *mouse_buffer = (uint8_t *)xhci_alloc_aligned(64, 64);

                        mouse_transfer_ring[0] = (uint32_t)(uintptr_t)mouse_buffer;
                        mouse_transfer_ring[1] = 0;
                        mouse_transfer_ring[2] = 4;
                        mouse_transfer_ring[3] = (1U << 10) | (1U << 5) | 1U; 

                        uint32_t dboff = xhci_caps->dboff & ~0x3U;
                        volatile uint32_t *doorbell_regs = (volatile uint32_t *)(uintptr_t)(xhci_mmio_base + dboff);
                        doorbell_regs[1] = 1; 
                        
                        serial_write("[xHCI] Endpoint Doorbell rung, mouse ready!\n");
                    }
                    xhci_driver_state = 4; // State 4: Fare aktif ve dinlemede
                }
            } else {
                serial_write("[xHCI] Command Completion failed with error code.\n");
            }
        } else if (trb_type == 34) { // Port Status Change Event
            serial_write("[xHCI] Port Status Change (34) caught!\n");

            uint8_t max_ports = (xhci_caps->hcsparams1 >> 24) & 0xFF;
            volatile uint32_t *ports = (volatile uint32_t *)(uintptr_t)((uintptr_t)xhci_op + 0x400);

            for (uint32_t p = 0; p < max_ports; p++) {
                uint32_t psc_val = ports[p * 4];
                
                if (psc_val & 2) { // CSC (Connection Status Change)
                    if (psc_val & 1) { 
                        serial_write("[xHCI] Device connected via PSC! Resetting port...\n");
                        ports[p * 4] = (psc_val & ~((1 << 9) | (1 << 10) | (1 << 11))) | (1 << 4) | (1 << 1);
                    } else {
                        ports[p * 4] = psc_val | (1 << 1);
                    }
                }
            }
        } else {
            serial_write("[xHCI] Other Event caught.\n");
        }

        // Index ilerlet ve Toggle Cycle kontrolü yap
        event_ring_index++;
        if (event_ring_index >= XHCI_EVENT_RING_TRBS) {
            event_ring_index = 0;
            event_ring_cs ^= 1;
        }

        // Dequeue Pointer (ERDP) güncellenerek kontrolcüye bildirilir (EHB bit 3 clear ve alt adres hizalaması)
        uint64_t dequeue = (uint64_t)(uintptr_t)global_event_ring + (event_ring_index * 16);
        runtime_regs[0x38 / 4] = (uint32_t)(dequeue & ~0x0FUL) | (1U << 3); 
        runtime_regs[0x3C / 4] = (uint32_t)(dequeue >> 32);

        processed_any = 1;
    }

    return processed_any;
}

int xhci_get_driver_state(void) {
    return xhci_driver_state;
}

uint8_t xhci_is_active(void) {
    return (global_event_ring != NULL) ? 1 : 0;
}