#include <kernel/drivers/usb/uhci.h>
#include <kernel/drivers/pci.h>
#include <kernel/serial.h>
#include <arch/x86/io.h>

// UHCI I/O Register Offsets
#define UHCI_USBCMD      0x00
#define UHCI_USBSTS      0x02
#define UHCI_USBINTR     0x04
#define UHCI_FRNUM       0x06
#define UHCI_FRBASEADD   0x08
#define UHCI_SOFMOD      0x0C
#define UHCI_PORTSC1     0x10
#define UHCI_PORTSC2     0x12

// USBCMD Command Bits
#define USBCMD_RS        (1 << 0) // Run / Stop
#define USBCMD_HCRESET   (1 << 1) // Host Controller Reset
#define USBCMD_GRESET    (1 << 2) // Global Reset

// PortSC Bits
#define PORTSC_CCS       (1 << 0) // Current Connect Status
#define PORTSC_CSC       (1 << 1) // Connect Status Change
#define PORTSC_PORT_EN   (1 << 2) // Port Enable
#define PORTSC_PEC       (1 << 3) // Port Enable Change
#define PORTSC_RESET     (1 << 9) // Port Reset
#define PORTSC_POW       (1 << 12)// Port Power

// Frame List Size (1024 32-bit pointers)
#define FRAME_LIST_COUNT 1024

static uint16_t uhci_io_base = 0;

// 4KB aligned Frame List
static uint32_t __attribute__((aligned(4096))) frame_list[FRAME_LIST_COUNT];

// UHCI I/O Helpers
static inline void uhci_outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void uhci_outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t uhci_inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void uhci_check_ports(void) {
    uint16_t ports[2] = { UHCI_PORTSC1, UHCI_PORTSC2 };

    for (int i = 0; i < 2; i++) {
        uint16_t port_reg = uhci_io_base + ports[i];
        uint16_t status = uhci_inw(port_reg);

        // 1. Ensure Port Power is enabled
        if (!(status & PORTSC_POW)) {
            serial_write("UHCI: Enabling power for port...\n");
            uhci_outw(port_reg, status | PORTSC_POW);
            for (volatile int d = 0; d < 50000; d++);
            status = uhci_inw(port_reg);
        }

        serial_write("UHCI: Checking Port...\n");
        
        if (status & PORTSC_CCS) {
            serial_write("UHCI: Device connected! Resetting port...\n");
            
            // In UHCI, CSC (bit 1) and PEC (bit 3) are W1C (Write 1 to Clear). 
            // When writing to PORTSC, we must write 1 to clear change bits if they are set, 
            // otherwise they might interfere with state changes.
            uint16_t reset_val = PORTSC_POW | PORTSC_RESET;
            if (status & PORTSC_CSC) reset_val |= PORTSC_CSC;
            if (status & PORTSC_PEC) reset_val |= PORTSC_PEC;
            
            uhci_outw(port_reg, reset_val);
            
            // Hold reset for ~50ms
            for (volatile int d = 0; d < 60000; d++);
            
            // Clear Reset bit, keep Power enabled, and clear any pending change bits
            uint16_t clear_reset_val = PORTSC_POW | PORTSC_CSC | PORTSC_PEC;
            uhci_outw(port_reg, clear_reset_val);
            
            // Wait for recovery (~20ms)
            for (volatile int d = 0; d < 20000; d++);
            
            // Check status again
            status = uhci_inw(port_reg);
            if (!(status & PORTSC_PORT_EN)) {
                // If port didn't enable automatically, explicitly try to enable it
                serial_write("UHCI: Port not enabled yet, forcing enable...\n");
                uhci_outw(port_reg, PORTSC_POW | PORTSC_PORT_EN | PORTSC_CSC | PORTSC_PEC);
                for (volatile int d = 0; d < 10000; d++);
                status = uhci_inw(port_reg);
            }

            if (status & PORTSC_PORT_EN) {
                serial_write("UHCI: Port successfully enabled and device ready!\n");
            } else {
                serial_write("UHCI: Port reset failed or device unsupported.\n");
            }
        } else {
            serial_write("UHCI: Port is empty.\n");
        }
    }
}

void uhci_init(void) {
    serial_write("UHCI: Initializing driver...\n");

    // Search for UHCI controller on the PCI bus using the new helper function
    pci_device_t* dev = pci_get_device_by_class(0x0C, 0x03, 0x00);

    if (!dev) {
        serial_write("UHCI: No UHCI controller found on PCI bus.\n");
        return;
    }

    serial_write("UHCI: Controller found, enabling PCI Bus Mastering...\n");
    pci_enable_bus_mastering(dev);

    // Read BAR4 address for UHCI I/O space (PIIX3 uses BAR4)
    uint32_t bar4 = dev->bar[4];
    if (bar4 & 0x01) {
        uhci_io_base = (uint16_t)(bar4 & ~0x03);
    } else {
        serial_write("UHCI Error: BAR4 is not configured as I/O space!\n");
        return;
    }

    serial_write("UHCI: I/O Base address successfully obtained.\n");

    // 1. Reset the Controller (Host Controller Reset)
    uhci_outw(uhci_io_base + UHCI_USBCMD, USBCMD_HCRESET);
    
    // Wait for the reset bit to clear
    for (volatile int i = 0; i < 10000; i++) {
        if (!(uhci_inw(uhci_io_base + UHCI_USBCMD) & USBCMD_HCRESET)) break;
    }

    // 2. Configure Frame List Memory (Terminate bit set)
    for (int i = 0; i < FRAME_LIST_COUNT; i++) {
        frame_list[i] = 1; // Terminate bit (T = 1)
    }
    uhci_outl(uhci_io_base + UHCI_FRBASEADD, (uint32_t)frame_list);
    uhci_outw(uhci_io_base + UHCI_FRNUM, 0);

    // 3. Start the Controller (Run/Stop)
    uhci_outw(uhci_io_base + UHCI_USBCMD, USBCMD_RS | (1 << 7));

    serial_write("UHCI: Controller successfully started (Running).\n");

    for (volatile int i = 0; i < 500000; i++);

    // Check port statuses
    uhci_check_ports();
}