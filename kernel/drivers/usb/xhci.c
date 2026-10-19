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
static uint32_t *global_ep0_transfer_ring = NULL;
static uint64_t *global_dcbaa = NULL;

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
    global_dcbaa = dcbaa; 
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

    // --- DOĞRUDAN MMIO OFSETLERİ İLE CRCR VE DCBAAP YAZIMI ---
    uint8_t *op_base = (uint8_t *)(uintptr_t)xhci_op;
    volatile uint64_t *reg_crcr   = (volatile uint64_t *)(op_base + 0x18); // CRCR Offset: 0x18
    volatile uint64_t *reg_dcbaap = (volatile uint64_t *)(op_base + 0x30); // DCBAAP Offset: 0x30

    *reg_dcbaap = (uint64_t)(uintptr_t)dcbaa;
    *reg_crcr   = (uint64_t)(uintptr_t)command_ring | 1U; // Adres + RCS (Bit 0)
    command_ring_cs = 1;
    
    // Bellek bariyeri (Memory fence)
    __asm__ volatile("mfence" ::: "memory");

    // ERST Tablosu (Event Ring Segment Table) yapılandırması
    erst[0] = (uint32_t)(uintptr_t)event_ring;
    erst[1] = 0;
    erst[2] = XHCI_EVENT_RING_TRBS;
    erst[3] = 0;

    uint32_t rtsoff = xhci_caps->rtsoff & ~0x1FU;
    volatile uint8_t *runtime_base = (volatile uint8_t *)(uintptr_t)(xhci_mmio_base + rtsoff);
    
    // Doğru Interrupter 0 Ofsetleri
    volatile uint32_t *iman0   = (volatile uint32_t *)(runtime_base + 0x20); // IMAN0
    volatile uint32_t *erstsz0 = (volatile uint32_t *)(runtime_base + 0x28); // ERSTSZ0 (0x28)
    volatile uint64_t *erstba0 = (volatile uint64_t *)(runtime_base + 0x30); // ERSTBA0 (0x30)
    volatile uint64_t *erdp0   = (volatile uint64_t *)(runtime_base + 0x38); // ERDP0   (0x38)

    *iman0   |= (1U << 1);                                    // IMAN0: Interrupt Enable (Bit 1)
    *erstsz0  = 1;                                           // ERSTSZ0: 1 segment
    *erstba0  = (uint64_t)(uintptr_t)erst;                   // ERSTBA0: Tablo fiziksel adresi
    *erdp0    = (uint64_t)(uintptr_t)event_ring | (1U << 3); // ERDP0: Dequeue pointer + EHB (Bit 3)

    xhci_op->usbcmd |= 1U;
    timeout = 1000000;
    while ((xhci_op->usbsts & 1) && --timeout) {}
    if (!timeout) {
        serial_write("[xHCI] ERROR: Controller failed to enter Run state.\n");
        global_event_ring = NULL;
        return;
    }

    serial_write("[xHCI] Controller running; event ring polling enabled.\n");
}

void xhci_send_command(uint32_t trb0, uint32_t trb1, uint32_t trb2, uint32_t trb3_type_and_flags) {
    if (!global_command_ring) return;

    if (command_ring_index >= 15) {
        volatile uint32_t *link_trb = (volatile uint32_t *)((uintptr_t)global_command_ring + (15 * 16));
        link_trb[0] = (uint32_t)(uintptr_t)global_command_ring;
        link_trb[1] = 0;
        link_trb[2] = 0;
        link_trb[3] = (6U << 10) | (1U << 1) | (uint32_t)command_ring_cs;

        command_ring_index = 0;
        command_ring_cs ^= 1; 
    }

    volatile uint32_t *cmd_trb = (volatile uint32_t *)((uintptr_t)global_command_ring + (command_ring_index * 16));
    
    // Önce TRB'nin ilk 3 DWORD alanını doldur
    cmd_trb[0] = trb0;
    cmd_trb[1] = trb1;
    cmd_trb[2] = trb2;
    
    // Cycle bitini içeren 4. DWORD'ü EN SON yazıyoruz (Önemli: Host kontrolcü cycle bitini en son değiştiğini görür)
    uint32_t final_trb3 = (trb3_type_and_flags & ~1U) | (uint32_t)(command_ring_cs & 1U);
    cmd_trb[3] = final_trb3;

    command_ring_index++;

    // Bellek Bariyeri (CPU Store Fence): TRB verilerinin RAM'e tamamen yazıldığını garanti eder
    __asm__ volatile("mfence" ::: "memory");

    // Host Controller Command Doorbell'ini çal (Target ID = 0)
    uint32_t dboff = xhci_caps->dboff & ~0x3U;
    volatile uint32_t *doorbell_regs = (volatile uint32_t *)(uintptr_t)(xhci_mmio_base + dboff);
    
    doorbell_regs[0] = 0; // Slot ID 0 = Command Ring
    
    // Yazma sonrası okuma posting (I/O write posting barrier)
    (void)doorbell_regs[0];

    serial_write("[xHCI] Command TRB written to ring and Host Controller Doorbell rung.\n");
}

uint8_t xhci_poll(void) {
    if (!global_event_ring) return 0;

    // --- GARANTİ PORT KONTROLÜ VE GÜÇ AÇMA ---
    if (xhci_driver_state == 0) {
        uint8_t max_ports = (xhci_caps->hcsparams1 & 0xFF);
        if (max_ports == 0) max_ports = 4; 

        volatile uint32_t *portsc_base = (volatile uint32_t *)(uintptr_t)((uintptr_t)xhci_op + 0x400);

        for (uint8_t i = 0; i < max_ports; i++) {
            volatile uint32_t *portsc = &portsc_base[i * 4];
            uint32_t status = *portsc;

            // 1. Adım: Port Power (PP - Bit 9) kapalıysa açalım
            if (!(status & (1 << 9))) {
                uint32_t port_val = status & ~(0x007E0000); // W1C bit maskesi temizlenir
                port_val |= (1 << 9); // PP = 1
                *portsc = port_val;
                continue;
            }

            // 2. Adım: Cihaz bağlı mı? (CCS - Bit 0)
            if (status & 1) {
                serial_write("[xHCI] Port found active device! Sending Enable Slot...\n");
                
                uint32_t port_val = status & ~(0x007E0000); 
                port_val |= (1 << 4) | (1 << 9); // PR (Port Reset) = 1, PP = 1
                *portsc = port_val;

                // Enable Slot Komutunu Gönder (TRB Type = 9)
                xhci_send_command(0, 0, 0, (9U << 10));
                xhci_driver_state = 1; 
                break;
            }
        }
    }

    uint8_t processed_any = 0;
    uint32_t rtsoff = xhci_caps->rtsoff & ~0x1FU;
    volatile uint8_t *runtime_base = (volatile uint8_t *)(uintptr_t)(xhci_mmio_base + rtsoff);
    volatile uint64_t *erdp0 = (volatile uint64_t *)(runtime_base + 0x38); // Doğru ofset: 0x38

    while (1) {
        volatile uint32_t *trb = (volatile uint32_t *)((uintptr_t)global_event_ring + event_ring_index * 16);
        uint32_t trb_3 = trb[3];

        if ((trb_3 & 1) != event_ring_cs) {
            break; 
        }

        uint32_t trb_type = (trb_3 >> 10) & 0x3F;
        if (trb_type == 0) {
            break;
        }

        // --- TEST: Gelen her event tipini görelim ---
        if (trb_type != 33 && trb_type != 34) {
            serial_write("[xHCI] Event caught, type: ");
            serial_write_dec(trb_type);
            serial_write("\n");
        }

        // 1. Komut Tamamlama Event'i (Command Completion Event - Type 33)
        if (trb_type == 33) { 
            uint32_t comp_code = (trb[2] >> 24) & 0xFF;
            uint32_t slot_id = (trb_3 >> 24) & 0xFF;
            
            if (comp_code == 1) { // Success
                serial_write("[xHCI] Command Completion (33) SUCCESS caught!\n");
                
                if (xhci_driver_state == 1) { // Enable Slot tamamlandı
                    allocated_slot_id = slot_id ? slot_id : 1;
                    serial_write("[xHCI] Enable Slot completed successfully. Sending Address Device...\n");
                    
                    uint32_t *device_context = (uint32_t *)xhci_alloc_aligned(2048, 64);
                    if (global_dcbaa && device_context) {
                        for (int i = 0; i < 512; i++) device_context[i] = 0;
                        global_dcbaa[allocated_slot_id] = (uint64_t)(uintptr_t)device_context;
                    }

                    uint32_t *ep0_transfer_ring = (uint32_t *)xhci_alloc_aligned(16 * 16, 64);
                    if (ep0_transfer_ring) {
                        for (int i = 0; i < 16 * 4; i++) ep0_transfer_ring[i] = 0;
                        ep0_transfer_ring[15 * 4] = (uint32_t)(uintptr_t)ep0_transfer_ring;
                        ep0_transfer_ring[15 * 4 + 3] = (6U << 10) | (1U << 1) | 1U; 
                        global_ep0_transfer_ring = ep0_transfer_ring;
                    }

                    uint32_t *input_context = (uint32_t *)xhci_alloc_aligned(2048, 64);
                    if (input_context) {
                        for (int i = 0; i < 512; i++) input_context[i] = 0;
                        
                        input_context[0] = 0x0; 
                        input_context[1] = 0x3; 
                        
                        input_context[8 + 0] = (3U << 20) | (2U << 27); 
                        input_context[8 + 1] = (1U << 16); 

                        input_context[16 + 0] = (4U << 3); 
                        input_context[16 + 1] = (64U << 16); 
                        input_context[16 + 2] = 8U; 
                        input_context[16 + 3] = (uint32_t)(uintptr_t)ep0_transfer_ring | 1U; 
                        input_context[16 + 4] = 0; 
                    }

                    uint32_t cmd_flags = (11U << 10) | ((uint32_t)allocated_slot_id << 24);
                    xhci_send_command((uint32_t)(uintptr_t)input_context, 0, 0, cmd_flags);
                    xhci_driver_state = 2; // State 2: Address Device bekleniyor
                } else if (xhci_driver_state == 2) { // Address Device tamamlandı
                    serial_write("[xHCI] Address Device completed successfully! Sending Configure Endpoint & Set Config...\n");
                    
                    // 1. EP0 Transfer Ring üzerine Setup ve Status TRB'lerini yazıyoruz
                    if (global_ep0_transfer_ring) {
                        global_ep0_transfer_ring[0] = (0x00U) | (0x09U << 8) | (1U << 16); // Setup: Set Configuration (wValue=1)
                        global_ep0_transfer_ring[1] = 0; 
                        global_ep0_transfer_ring[2] = 8U; 
                        global_ep0_transfer_ring[3] = (2U << 10) | (0U << 16) | (1U << 5) | 1U; // TRT=0, Chain=1, Cycle=1

                        global_ep0_transfer_ring[4] = 0;
                        global_ep0_transfer_ring[5] = 0;
                        global_ep0_transfer_ring[6] = 0;
                        global_ep0_transfer_ring[7] = (4U << 10) | (1U << 16) | (1U << 2) | 1U; // Status TRB, IOC=1, Cycle=1
                    }

                    // 2. Endpoint 1 (Fare) için Transfer Ring oluşturuluyor
                    uint32_t *mouse_transfer_ring = (uint32_t *)xhci_alloc_aligned(16 * 16, 64);
                    if (mouse_transfer_ring) {
                        global_mouse_transfer_ring = mouse_transfer_ring;
                        uint8_t *mouse_buffer = (uint8_t *)xhci_alloc_aligned(64, 64);
                        for(int i=0; i<16*4; i++) mouse_transfer_ring[i] = 0;

                        // İlk Transfer TRB'si (Normal TRB - Type 1)
                        mouse_transfer_ring[0] = (uint32_t)(uintptr_t)mouse_buffer; // Data Buffer Ptr Low
                        mouse_transfer_ring[1] = 0;                                 // Data Buffer Ptr High
                        mouse_transfer_ring[2] = 8;                                 // Transfer Length = 8 bytes
                        mouse_transfer_ring[3] = (1U << 10) | (1U << 5) | 1U;       // TRB Type = Normal (1), IOC = 1, Cycle = 1

                        // ÖNEMLİ: Ring'in devamında başa saran Link TRB (Type 6)
                        mouse_transfer_ring[1 * 4 + 0] = (uint32_t)(uintptr_t)mouse_transfer_ring;
                        mouse_transfer_ring[1 * 4 + 1] = 0;
                        mouse_transfer_ring[1 * 4 + 2] = 0;
                        mouse_transfer_ring[1 * 4 + 3] = (6U << 10) | (1U << 1) | 1U; // Link TRB, TC=1, Cycle=1
                    }

                    // 3. Configure Endpoint için Input Context Hazırlanıp Komut Gönderiliyor
                    uint32_t *input_context = (uint32_t *)xhci_alloc_aligned(2048, 64);
                    if (input_context && mouse_transfer_ring) {
                        for (int i = 0; i < 512; i++) input_context[i] = 0;
                        
                        // Input Control Context (ICC)
                        input_context[0] = 0x0; // Drop Flags
                        input_context[1] = 0x9; // Add Flags: EP0 (bit 0) + EP1 IN (bit 3) = 0x9
                        
                        // Slot Context (Index 8)
                        input_context[8 + 0] = (3U << 27); // Context Entries = 3
                        input_context[8 + 1] = (1U << 16); // Speed = Full/High

                        // Endpoint 0 Context (Index 16)
                        input_context[16 + 0] = (4U << 3);  // Endpoint Type = Control (4)
                        input_context[16 + 1] = (64U << 16); // Max Packet Size = 64
                        input_context[16 + 2] = 8U; 
                        input_context[16 + 3] = (uint32_t)(uintptr_t)global_ep0_transfer_ring | 1U; 

                        // Endpoint 1 IN (Mouse Interrupt) Context (Index 32)
                        // ÖNEMLİ: Interval ve Max Burst Size ayarları düzeltildi
                        input_context[32 + 0] = (7U << 3) | (3U << 16); // Endpoint Type = Interrupt IN (7), Max Burst = 0
                        input_context[32 + 1] = (8U << 16) | (3U << 1); // Max Packet Size = 8, Interval = 4 (2^3 = 8ms)
                        input_context[32 + 2] = (3U << 1) | (1U << 0); // Error Count = 3, Dequeue Cycle State (DCS) = 1
                        input_context[32 + 3] = (uint32_t)(uintptr_t)mouse_transfer_ring | 1U; // Dequeue Pointer + DCS=1
                    }

                    uint32_t dboff = xhci_caps->dboff & ~0x3U;
                    volatile uint32_t *doorbell_regs = (volatile uint32_t *)(uintptr_t)(xhci_mmio_base + dboff);
                    
                    __asm__ volatile("mfence" ::: "memory");
                    
                    // Configure Endpoint Komutunu Gönder (TRB Type = 12)
                    uint32_t cmd_flags = (12U << 10) | ((uint32_t)allocated_slot_id << 24);
                    xhci_send_command((uint32_t)(uintptr_t)input_context, 0, 0, cmd_flags);

                    // EP0 Doorbell'i de çalıyoruz
                    doorbell_regs[allocated_slot_id] = 1; 
                    
                    serial_write("[xHCI] Configure Endpoint command sent for Mouse and EP0!\n");
                    xhci_driver_state = 3; // State 3: Komut tamamlanması bekleniyor
                } else if (xhci_driver_state == 3) { // Configure Endpoint komutu BAŞARIYLA bitti!
                    serial_write("[xHCI] Configure Endpoint completed successfully! Initializing Mouse ring...\n");
                    
                    // Fare transfer ring'ine ilk okuma TRB'sini yazıyoruz
                    if (global_mouse_transfer_ring) {
                        uint8_t *mouse_buffer = (uint8_t *)xhci_alloc_aligned(64, 64);
                        if (mouse_buffer) {
                            global_mouse_transfer_ring[0] = (uint32_t)(uintptr_t)mouse_buffer;
                            global_mouse_transfer_ring[1] = 0;
                            global_mouse_transfer_ring[2] = 8; // Tablet rapor boyutu genellikle 5-8 bayttır
                            global_mouse_transfer_ring[3] = (1U << 10) | (1U << 5) | 1U; // Normal TRB (Tip 1), IOC=1, Cycle=1
                        }
                    }

                    // Endpoint 1 Doorbell'ini çalarak fare verilerini dinlemeye başlıyoruz
                    uint32_t dboff = xhci_caps->dboff & ~0x3U;
                    volatile uint32_t *doorbell_regs = (volatile uint32_t *)(uintptr_t)(xhci_mmio_base + dboff);
                    doorbell_regs[allocated_slot_id] = 3; // Endpoint 1 IN Doorbell (Endpoint ID 3)
                    
                    serial_write("[xHCI] Mouse Endpoint Doorbell rung (ID: 3), waiting for input events...\n");
                    xhci_driver_state = 4; // State 4: Fare aktif ve veri dinliyor
                }
            } else {
                if (comp_code == 2) serial_write("[xHCI] Error: Data Buffer Error (2)\n");
                else if (comp_code == 3) serial_write("[xHCI] Error: Babble Detected Error (3)\n");
                else if (comp_code == 4) serial_write("[xHCI] Error: USB Transaction Error (4)\n");
                else if (comp_code == 5) serial_write("[xHCI] Error: TRB Error (5)\n");
                else if (comp_code == 6) serial_write("[xHCI] Error: Stall Error (6)\n");
                else if (comp_code == 13) serial_write("[xHCI] Error: Context State Error (13)\n");
                else if (comp_code == 17) serial_write("[xHCI] Error: Parameter Error (17)\n");
                else if (comp_code == 19) serial_write("[xHCI] Error: Slot Not Enabled Error (19)\n");
                else serial_write("[xHCI] Command Completion failed with unknown code!\n");
            }

        // 2. Transfer Event'i (Transfer Event - Type 32)
        } else if (trb_type == 32) { 
            uint32_t comp_code = (trb[2] >> 24) & 0xFF;
            uint32_t ep_id = (trb[3] >> 16) & 0x1F; // Hangi Endpoint? (EP0 = 1, EP1 = 3 vb.)
            
            serial_write("[xHCI] Transfer Event (32) caught! CompCode: ");
            serial_write_dec(comp_code);
            serial_write(" | EP ID: ");
            serial_write_dec(ep_id);
            serial_write(" | State: ");
            serial_write_dec(xhci_driver_state);
            serial_write("\n");
            
            if (xhci_driver_state == 3 && comp_code == 1) { 
                serial_write("[xHCI] Set Configuration transfer completed successfully! Initializing Mouse Interrupt Ring...\n");
                
                uint32_t *mouse_transfer_ring = (uint32_t *)xhci_alloc_aligned(16 * 16, 64);
                if (mouse_transfer_ring) {
                    global_mouse_transfer_ring = mouse_transfer_ring;
                    uint8_t *mouse_buffer = (uint8_t *)xhci_alloc_aligned(64, 64);

                    mouse_transfer_ring[0] = (uint32_t)(uintptr_t)mouse_buffer;
                    mouse_transfer_ring[1] = 0;
                    mouse_transfer_ring[2] = 8; 
                    mouse_transfer_ring[3] = (1U << 10) | (1U << 5) | 1U; // Normal TRB, IOC=1, Cycle=1

                    uint32_t dboff = xhci_caps->dboff & ~0x3U;
                    volatile uint32_t *doorbell_regs = (volatile uint32_t *)(uintptr_t)(xhci_mmio_base + dboff);
                    doorbell_regs[allocated_slot_id] = 3; // Endpoint 1 IN Doorbell (Endpoint ID 3)
                    
                    serial_write("[xHCI] Mouse Endpoint Doorbell rung (ID: 3)!\n");
                }
                xhci_driver_state = 4; // State 4: Fare aktif
            } else if (xhci_driver_state >= 4 && comp_code == 1) {
                if (global_mouse_transfer_ring) {
                    uint8_t *mouse_buffer = (uint8_t *)(uintptr_t)global_mouse_transfer_ring[0];
                    if (mouse_buffer) {
                        usb_mouse_process_report(mouse_buffer, 8);
                    }

                    // Kuyruğu tazeleyip Doorbell'i tekrar çal
                    uint8_t *new_buffer = (uint8_t *)xhci_alloc_aligned(64, 64);
                    if (new_buffer) {
                        global_mouse_transfer_ring[0] = (uint32_t)(uintptr_t)new_buffer;
                        global_mouse_transfer_ring[1] = 0;
                        global_mouse_transfer_ring[2] = 8;
                        global_mouse_transfer_ring[3] = (1U << 10) | (1U << 5) | 1U;

                        // Link TRB her tazelemede güncel tutulur
                        global_mouse_transfer_ring[1 * 4 + 0] = (uint32_t)(uintptr_t)global_mouse_transfer_ring;
                        global_mouse_transfer_ring[1 * 4 + 1] = 0;
                        global_mouse_transfer_ring[1 * 4 + 2] = 0;
                        global_mouse_transfer_ring[1 * 4 + 3] = (6U << 10) | (1U << 1) | 1U;

                        uint32_t dboff = xhci_caps->dboff & ~0x3U;
                        volatile uint32_t *doorbell_regs = (volatile uint32_t *)(uintptr_t)(xhci_mmio_base + dboff);
                        
                        __asm__ volatile("mfence" ::: "memory");
                        doorbell_regs[allocated_slot_id] = 3; 
                    }
                }
            }

        // 3. Port Statü Değişikliği Event'i (Port Status Change Event - Type 34)
        } else if (trb_type == 34) {
            serial_write("[xHCI] Port Status Change (34) caught!\n");
            
            uint32_t port_id = (trb[0] >> 24) & 0xFF;
            if (port_id == 0) port_id = 1;
            
            volatile uint32_t *portsc = (volatile uint32_t *)(uintptr_t)((uintptr_t)xhci_op + 0x400 + (port_id - 1) * 16);
            uint32_t status = *portsc;

            if ((status & 1) && xhci_driver_state == 0) {
                serial_write("[xHCI] Device detected via PSC, resetting port...\n");
                
                uint32_t port_val = status & ~(0x007E0000); 
                port_val |= (1 << 4) | (1 << 9); 
                *portsc = port_val;

                xhci_send_command(0, 0, 0, (9U << 10)); // Enable Slot
                xhci_driver_state = 1; 
            }
        } else {
            serial_write("[xHCI] Other Event caught.\n");
        }

        event_ring_index++;
        if (event_ring_index >= XHCI_EVENT_RING_TRBS) {
            event_ring_index = 0;
            event_ring_cs ^= 1;
        }

        uint64_t dequeue = (uint64_t)(uintptr_t)global_event_ring + (event_ring_index * 16);
        *erdp0 = dequeue | (1U << 3); 

        processed_any = 1;
    }

    // Eğer fare sürücüsü State 4'te aktifse ve ring kurulmuşsa, 
    // veri akışını tetiklemek için doorbell'i periyodik olarak yoklayabiliriz
    if (xhci_driver_state == 4 && global_mouse_transfer_ring) {
        static uint32_t poll_counter = 0;
        if (++poll_counter > 500000) { // Her 500 bin döngüde bir
            uint32_t dboff = xhci_caps->dboff & ~0x3U;
            volatile uint32_t *doorbell_regs = (volatile uint32_t *)(uintptr_t)(xhci_mmio_base + dboff);
            
            __asm__ volatile("mfence" ::: "memory");
            doorbell_regs[allocated_slot_id] = 3; // Endpoint 1 Doorbell tekrar çalınır
            poll_counter = 0;
        }
    }

    return processed_any;
}

int xhci_get_driver_state(void) {
    return xhci_driver_state;
}

uint8_t xhci_is_active(void) {
    return (global_event_ring != NULL) ? 1 : 0;
}