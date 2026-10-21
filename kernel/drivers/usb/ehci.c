#include <kernel/drivers/usb/ehci.h>
#include <kernel/drivers/pci.h>
#include <kernel/serial.h>
#include <kernel/mem/heap.h>
#include <kernel/drivers/input/mouse_usb.h>
#include <stdint.h>
#include <stddef.h>

// EHCI Capability Registers
typedef struct {
    volatile uint8_t caplength;
    volatile uint8_t reserved;
    volatile uint16_t hciversion;
    volatile uint32_t hcsparams;
    volatile uint32_t hccparams;
    volatile uint32_t portroute[2];
} __attribute__((packed)) ehci_caps_t;

// EHCI Operational Registers
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

// EHCI Queue Element Transfer Descriptor (qTD)
typedef struct {
    volatile uint32_t next_td;
    volatile uint32_t alt_next_td;
    volatile uint32_t token;
    volatile uint32_t buffer[5];
} __attribute__((packed, aligned(32))) ehci_qtd_t;

// EHCI Queue Head (QH)
typedef struct {
    volatile uint32_t horizontal_link;
    volatile uint32_t ep_chars;
    volatile uint32_t ep_caps;
    volatile uint32_t current_qtd;
    volatile uint32_t next_qtd;
    volatile uint32_t alt_next_qtd;
    volatile uint32_t token;
    volatile uint32_t buffer[5];
} __attribute__((packed, aligned(32))) ehci_qh_t;

static volatile ehci_caps_t *ehci_caps;
static volatile ehci_op_regs_t *ehci_op;
static uint64_t ehci_mmio_base = 0;
static uint8_t ehci_active = 0;

// Periyodik Liste ve Fare Yapıları
static uint32_t *periodic_frame_list = NULL;
static ehci_qh_t *mouse_qh = NULL;
static ehci_qtd_t *mouse_qtd = NULL;
static uint8_t *mouse_data_buf = NULL;

static inline uint32_t virt_to_phys(volatile void* virt_addr) {
    return (uint32_t)(uintptr_t)virt_addr;
}

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
    serial_write("[EHCI-DEBUG] Initializing EHCI driver (USB 2.0 with Periodic List)...\n");

    pci_device_t *pci_dev = pci_get_device_by_class(EHCI_CLASS_CODE, EHCI_SUBCLASS, EHCI_PROG_IF);
    if (!pci_dev) {
        serial_write("[EHCI-DEBUG] ERROR: EHCI controller not found on PCI bus!\n");
        return;
    }

    pci_enable_bus_mastering(pci_dev);

    uint32_t bar0 = pci_dev->bar[0];
    if (bar0 == 0 || (bar0 & 1)) {
        serial_write("[EHCI-DEBUG] ERROR: Invalid EHCI MMIO BAR0.\n");
        return;
    }

    ehci_mmio_base = bar0 & 0xFFFFFFF0;
    ehci_caps = (volatile ehci_caps_t *)(uintptr_t)ehci_mmio_base;
    
    if (ehci_caps->caplength < sizeof(ehci_caps_t)) {
        serial_write("[EHCI-DEBUG] ERROR: Invalid capability register length.\n");
        return;
    }
    
    ehci_op = (volatile ehci_op_regs_t *)(uintptr_t)(ehci_mmio_base + ehci_caps->caplength);

    // Debug: HCSPARAMS ve HCCPARAMS değerlerini basalım
    serial_write("[EHCI-DEBUG] HCSPARAMS: 0x");
    serial_write_hex(ehci_caps->hcsparams);
    serial_write(" | HCCPARAMS: 0x");
    serial_write_hex(ehci_caps->hccparams);
    serial_write("\n");

    // 1. Controller Halt
    ehci_op->usbcmd &= ~1U;
    uint32_t timeout = 1000000;
    while (!(ehci_op->usbsts & (1 << 12)) && --timeout) {}
    if (!timeout) {
        serial_write("[EHCI-DEBUG] ERROR: Controller did not halt.\n");
        return;
    }

    // 2. Controller Reset
    ehci_op->usbcmd |= (1 << 1);
    timeout = 1000000;
    while ((ehci_op->usbcmd & (1 << 1)) && --timeout) {}
    if (!timeout) {
        serial_write("[EHCI-DEBUG] ERROR: Controller reset timed out.\n");
        return;
    }

    // 3. Port Routing ve Companion Port Sahipliğini Devralma (Port Owner = 0)
    ehci_op->configflag = 1;

    uint8_t n_ports = (ehci_caps->hcsparams & 0x0F);
    
    // PORSC register'ları Operational register başlangıcından itibaren 0x44 offsetindedir
    volatile uint32_t *portsc = (volatile uint32_t *)((uintptr_t)ehci_op + 0x44);

    serial_write("[EHCI-DEBUG] Total Ports Detected: ");
    serial_write_dec(n_ports);
    serial_write("\n");

    for (uint8_t i = 0; i < n_ports; i++) {
        uint32_t val = portsc[i];
        serial_write("[EHCI-DEBUG] Port ");
        serial_write_dec(i);
        serial_write(" PORSC Value: 0x");
        serial_write_hex(val);
        serial_write("\n");

        // Companion kontrolcüden sahipliği EHCI'ye devret
        if (val & (1 << 13)) {
            val &= ~(1 << 13);
            portsc[i] = val;
            serial_write("[EHCI-DEBUG] -> Port owner switched to EHCI.\n");
        }

        // Eğer porta cihaz bağlıysa (Connect Status = Bit 0) reset atalım
        if (val & (1 << 0)) {
            serial_write("[EHCI-DEBUG] -> Resetting port ");
            serial_write_dec(i);
            serial_write("...\n");
            
            // Port Reset başlat (Bit 8 = 1), diğer statik bitleri bozmamak için dikkat edelim
            uint32_t sc = portsc[i];
            // Clear change bits (CSC=1, PEC=1) by writing 1 to them, and set Port Reset (Bit 8)
            portsc[i] = (sc & ~((1 << 1) | (1 << 3))) | (1 << 8);
            
            for (volatile int d = 0; d < 400000; d++); // Reset süresi
            
            // Reset bitini kapat ve portu enable etmeye çalış
            sc = portsc[i];
            portsc[i] = (sc & ~((1 << 8) | (1 << 1) | (1 << 3))) | (1 << 1) | (1 << 3); // Change bitlerini temizle
            
            for (volatile int d = 0; d < 50000; d++);
        }
        
        serial_write("[EHCI-DEBUG] -> Port status after reset. New PORSC: 0x");
        serial_write_hex(portsc[i]);
        serial_write("\n");
    }

    // 4. Periyodik Liste ve Fare Bellek Tahsisleri
    periodic_frame_list = (uint32_t *)ehci_alloc_aligned(1024 * sizeof(uint32_t), 4096);
    mouse_qh = (ehci_qh_t *)ehci_alloc_aligned(sizeof(ehci_qh_t), 32);
    mouse_qtd = (ehci_qtd_t *)ehci_alloc_aligned(sizeof(ehci_qtd_t), 32);
    mouse_data_buf = (uint8_t *)ehci_alloc_aligned(64, 32);

    if (periodic_frame_list && mouse_qh && mouse_qtd && mouse_data_buf) {
        // Fare QH Karakterleri:
        // - Device Address: 0 (bit 0-6) -> (0 << 0) [Henüz enumasyon yapılmadığı için adres 0]
        // - Endpoint Number: 1 (bit 8-11) -> (1 << 8)
        // - Endpoint Speed: Full-Speed (bit 12-13 = 0)
        // - Maximum Packet Size: 8 bayt (bit 16-26) -> (8 << 16)
        mouse_qh->horizontal_link = 1; // Terminate
        mouse_qh->ep_chars = (0 << 0) | (1 << 8) | (0 << 12) | (8 << 16);
        
        // Interrupt Schedule Mask & Split Transaction Capabilities:
        // - Interrupt Schedule Mask (bit 0-7): 0xFF (Tüm mikro-çerçeveler)
        // - Port Number (bit 8-11): 0 (Kök port 0)
        // - Hub Address (bit 16-22): 1 (USB Hub adresi)
        // - Split Completion Mask (C-mask, bit 16-23 of ep_caps alt alanı / split micro-frame window): 0x1C (Mikro-çerçeve 2, 3, 4)
        mouse_qh->ep_caps = (0xFF << 0) | (0 << 8) | (1 << 12) | (1 << 19) | (0x1C << 23);
        mouse_qh->current_qtd = virt_to_phys(mouse_qtd);

        // Fare qTD Ayarları (Active=bit7, IOC=bit15, PID IN=bit8-9)
        mouse_qtd->next_td = 1;      
        mouse_qtd->alt_next_td = 1;
        mouse_qtd->token = (8 << 16) | (1 << 15) | (3 << 10) | (1 << 8) | (1 << 7); // Length=8, IOC=1, PID=IN, Active=1
        mouse_qtd->buffer[0] = virt_to_phys(mouse_data_buf);

        mouse_qh->next_qtd = virt_to_phys(mouse_qtd);
        mouse_qh->alt_next_qtd = 1;
        mouse_qh->token = 0;

        // Frame Listesini Fare QH'sine Yönlendir (Type QH: adres | 2)
        uint32_t qh_ptr = virt_to_phys(mouse_qh) | 2;
        for (int i = 0; i < 1024; i++) {
            periodic_frame_list[i] = qh_ptr;
        }

        ehci_op->periodiclistbase = virt_to_phys(periodic_frame_list);

        serial_write("[EHCI-DEBUG] Periodic List Base set with Full-Speed QH on Port 0 Hub.\n");
    }

    // 5. Denetleyiciyi Çalıştır (Run, Periodic Schedule Enable, Interrupt Enable)
    ehci_op->usbcmd = (1U << 0) | (1U << 4) | (1U << 2) | (0x10 << 16);

    ehci_active = 1;
    serial_write("[EHCI-DEBUG] Controller running and fully initialized.\n");
}

uint8_t ehci_poll(void) {
    if (!ehci_active) return 0;

    uint32_t current_token = mouse_qtd->token;
    
    // Eğer qTD Halt olduysa veya hata aldıysa (Bit 6 = Halted, ya da hata bitleri)
    if (current_token & (1 << 6)) {
        serial_write("[EHCI-MOUSE] HALT/Error detected! Token: 0x");
        serial_write_hex(current_token);
        serial_write(" | Resetting qTD...\n");
        
        mouse_qtd->token = (8 << 16) | (1 << 15) | (3 << 10) | (1 << 8) | (1 << 7);
        mouse_qh->token = 0;
        mouse_qh->current_qtd = 0;
        mouse_qh->next_qtd = virt_to_phys(mouse_qtd);
        return 0;
    }

    // qTD Active biti (Bit 7) düşmüş mü?
    if (!(current_token & (1 << 7))) {
        serial_write("[EHCI-MOUSE] SUCCESS! Packet received from EHCI periodic list!\n");
        
        serial_write("[EHCI-MOUSE] Data -> Buttons: 0x");
        serial_write_hex8(mouse_data_buf[0]);
        serial_write(" | X: 0x");
        serial_write_hex8(mouse_data_buf[1]);
        serial_write(" | Y: 0x");
        serial_write_hex8(mouse_data_buf[2]);
        serial_write("\n");

        usb_mouse_process_report(mouse_data_buf, 8);
        
        // qTD'yi tekrar Active yap
        mouse_qtd->token = (8 << 16) | (1 << 15) | (3 << 10) | (1 << 8) | (1 << 7);
        mouse_qtd->buffer[0] = virt_to_phys(mouse_data_buf);
        mouse_qtd->next_td = 1;
        mouse_qtd->alt_next_td = 1;

        mouse_qh->current_qtd = 0;
        mouse_qh->next_qtd = virt_to_phys(mouse_qtd);
        mouse_qh->token = 0;

        return 1;
    }

    return 0;
}

uint8_t ehci_is_active(void) {
    return ehci_active;
}