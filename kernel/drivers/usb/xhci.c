#include <kernel/drivers/usb/xhci.h>
#include <kernel/drivers/pci.h>
#include <kernel/serial.h>
#include <kernel/mem/vmm.h>
#include <stdint.h>
#include <stddef.h>

// Dışarıdan USB fare rapor işleme fonksiyonunun prototipi
extern void usb_mouse_process_report(const uint8_t* report, uint8_t length);

// xHCI Class Codes (PCI)
#define XHCI_CLASS_CODE    0x0C
#define XHCI_SUBCLASS      0x03
#define XHCI_PROG_IF       0x30

// xHCI Register Structures
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
    serial_write("[xHCI] Initializing xHCI driver...\n");

    // 1. Find xHCI controller via PCI (Class: 0x0C, Subclass: 0x03, Prog IF: 0x30)
    pci_device_t *pci_dev = pci_get_device_by_class(XHCI_CLASS_CODE, XHCI_SUBCLASS, XHCI_PROG_IF);
    if (!pci_dev) {
        serial_write("[xHCI] ERROR: xHCI controller not found on PCI bus!\n");
        return;
    }

    serial_write("[xHCI] xHCI controller found.\n");
    
    // 2. Enable Bus Mastering and Memory Access
    pci_enable_bus_mastering(pci_dev);

    // 3. Get BAR0 Address (xHCI MMIO base is typically in BAR0)
    uint32_t bar0 = pci_dev->bar[0];
    if (bar0 == 0) {
        serial_write("[xHCI] ERROR: Invalid xHCI BAR0 address!\n");
        return;
    }

    // Clean physical address (lower bits are used for flags)
    uint32_t bar_phys = bar0 & 0xFFFFFFF0;
    xhci_mmio_base = (uint64_t)bar_phys;

    xhci_caps = (volatile xhci_caps_t *)(uintptr_t)xhci_mmio_base;
    uint8_t caplength = xhci_caps->caplength;

    serial_write("[xHCI] xHCI version and capabilities read successfully.\n");

    // Operational Registers = MMIO Base + CAPLENGTH
    xhci_op = (volatile xhci_op_regs_t *)(uintptr_t)(xhci_mmio_base + caplength);

    // 4. Stop the Controller (Run/Stop = 0)
    if (!(xhci_op->usbsts & (1 << 0))) { // If not HCHalted, stop it
        xhci_op->usbcmd &= ~1; // RS = 0
        int timeout = 1000;
        while (!(xhci_op->usbsts & (1 << 0)) && --timeout > 0);
    }

    // 5. Reset the Controller (HCRST)
    xhci_op->usbcmd |= (1 << 1); // HCRST = 1
    int timeout = 1000;
    while ((xhci_op->usbcmd & (1 << 1)) && --timeout > 0);

    if (timeout == 0) {
        serial_write("[xHCI] ERROR: xHCI reset timed out!\n");
        return;
    }

    // 6. Configure Max Slots (CONFIG Register)
    uint32_t hccparams1 = xhci_caps->hccparams1;
    uint8_t max_slots = hccparams1 & 0xFF;
    if (max_slots == 0) max_slots = 1;
    
    xhci_op->config = (xhci_op->config & ~0xFF) | max_slots;
    serial_write("[xHCI] Max Slots enabled.\n");

    // 6.1. Allocate and Set DCBAAP (Device Context Base Address Array Pointer)
    uint64_t *dcbaap = (uint64_t *)kzalloc((max_slots + 1) * sizeof(uint64_t));
    if (dcbaap) {
        xhci_op->dcbaap = (uint64_t)(uintptr_t)dcbaap;
        serial_write("[xHCI] DCBAAP allocated and set successfully.\n");
    } else {
        serial_write("[xHCI] ERROR: Failed to allocate memory for DCBAAP!\n");
        return;
    }

    // 6.2. Allocate and Set Command Ring (CRCR)
    // Her biri 16 bayt olan 16 adet TRB'lik komut halkası
    void *command_ring = kzalloc(16 * 16);
    if (command_ring) {
        // CRCR register'ına adres yazılır ve RCS (Ring Cycle State - Bit 0) 1 yapılır
        xhci_op->crcr = (uint64_t)(uintptr_t)command_ring | 1;
        serial_write("[xHCI] Command Ring allocated and set successfully.\n");
    } else {
        serial_write("[xHCI] ERROR: Failed to allocate memory for Command Ring!\n");
        return;
    }

    // 7. Start the Controller (Run/Stop = 1)
    xhci_op->usbcmd |= (1 << 0); // RS = 1
    serial_write("[xHCI] Controller set to Run mode.\n");

    // 8. Scan Ports and Check Connected Devices (Mouse)
    // xHCI port registers start at operational base + 0x400 offset
    volatile uint32_t *port_sc_base = (volatile uint32_t *)(uintptr_t)((uintptr_t)xhci_op + 0x400);
    
    // Get max ports from HCSPARAMS1 (upper 8 bits)
    uint32_t hcsparams1 = *((volatile uint32_t *)(uintptr_t)xhci_mmio_base);
    uint8_t max_ports = (hcsparams1 >> 24) & 0xFF;
    if (max_ports == 0) max_ports = 4;

    for (int i = 0; i < max_ports; i++) {
        volatile uint32_t *portsc_reg = &port_sc_base[i * 4]; 
        uint32_t portsc = *portsc_reg;
        
        // CCS (Current Connect Status) - Is a device connected? (Bit 0)
        if (portsc & (1 << 0)) {
            serial_write("[xHCI] Device detected on port!\n");
            
            // If port is not enabled (Enabled - Bit 1), reset the port (PR - Bit 4)
            if (!(portsc & (1 << 1))) {
                serial_write("[xHCI] Resetting port...\n");
                *portsc_reg = (portsc & ~(0x1F << 5)) | (1 << 4);
                
                int reset_timeout = 500000;
                while (!(*portsc_reg & (1 << 1)) && --reset_timeout > 0);
                
                if (*portsc_reg & (1 << 1)) {
                    serial_write("[xHCI] Port successfully enabled!\n");
                    
                    // Enable Slot Komutu TRB'si Hazırlanıyor (TRB Type = 9)
                    volatile uint32_t *cmd_ring = (volatile uint32_t *)(uintptr_t)(xhci_op->crcr & ~0x3F);
                    if (cmd_ring) {
                        cmd_ring[0] = 0;          // DWORD 0: Parameter Low
                        cmd_ring[1] = 0;          // DWORD 1: Parameter High
                        cmd_ring[2] = 0;          // DWORD 2: Status / Reserved
                        cmd_ring[3] = (9 << 10) | 1; // DWORD 3: TRB Type (9) ve Cycle Bit (1)
                        
                        serial_write("[xHCI] Enable Slot command written to Command Ring.\n");
                        
                        // Doorbell tetikleniyor
                        uint32_t dboff = xhci_caps->dboff;
                        volatile uint32_t *doorbell_base = (volatile uint32_t *)(uintptr_t)(xhci_mmio_base + dboff);
                        
                        doorbell_base[0] = 0; // Host Controller Command Ring (Index 0)
                        serial_write("[xHCI] Doorbell rung for Host Controller Command Ring.\n");
                    }
                } else {
                    serial_write("[xHCI] WARNING: Port enable timed out.\n");
                }
            } else {
                serial_write("[xHCI] Port is already active.\n");
            }
        }
    }

    serial_write("[xHCI] xHCI successfully initialized and ports scanned!\n");
}

uint8_t xhci_poll(void) {
    volatile uint32_t *port_sc_base = (volatile uint32_t *)(uintptr_t)((uintptr_t)xhci_op + 0x400);
    uint32_t portsc = port_sc_base[0];
    
    // Cihaz bağlı (CCS) ve port aktif (Enabled) durumdaysa
    if ((portsc & (1 << 0)) && (portsc & (1 << 1))) {
        // Test amaçlı veya ara geçiş aşamasında imlecin hareket döngüsünü 
        // tetiklemek için veri paketi işleme yapısı buraya eklenecektir.
        
        /*
        uint8_t mouse_packet[3] = {0};
        // usb_mouse_process_report(mouse_packet, 3);
        // return 1;
        */

        return 0;
    }
    
    return 0;
}