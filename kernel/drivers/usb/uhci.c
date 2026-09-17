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

// TD Status Bits
#define TD_STAT_ACTIVED      (1 << 23) // Active bit
#define TD_STAT_IOC          (1 << 24) // Interrupt on Completion
#define TD_STAT_SPD          (1 << 29) // Short Packet Detect

// TD Token Packet Identifiers (PID)
#define USB_PID_SETUP        0x2D
#define USB_PID_IN           0x69
#define USB_PID_OUT          0xE1

// Frame List Size (1024 32-bit pointers)
#define FRAME_LIST_COUNT 1024

static uint16_t uhci_io_base = 0;

// 4KB aligned Frame List
static uint32_t __attribute__((aligned(4096))) frame_list[FRAME_LIST_COUNT];

// Setup packet structure for USB control transfers
typedef struct {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_packet_t;

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

// Virtual to Physical address conversion helper for DMA
static inline uint32_t virt_to_phys(volatile void* virt_addr) {
    // KryonOS uses full 4GB identity mapping (VMM large pages), 
    // so virtual and physical addresses are identical, but wrapped for safety.
    return (uint32_t)virt_addr;
}

// Global aligned DMA buffers and descriptors for UHCI control transfers
static uhci_qh_t control_qh __attribute__((aligned(16)));
static uhci_td_t setup_td __attribute__((aligned(16))) ;
static uhci_td_t in_td __attribute__((aligned(16)));
static uhci_td_t status_td __attribute__((aligned(16)));
static usb_setup_packet_t setup_pkt __attribute__((aligned(4)));

static uint8_t device_descriptor[18] __attribute__((aligned(16)));

static void uhci_set_address(uint8_t port_index, uint8_t new_address) {
    (void)port_index;
    serial_write("UHCI: Setting USB device address to 1...\n");

    // 1. Prepare the 8-byte setup packet for SET_ADDRESS
    setup_pkt.bmRequestType = 0x00; // Host-to-device, Standard, Recipient: Device
    setup_pkt.bRequest      = 0x05; // SET_ADDRESS
    setup_pkt.wValue        = new_address; // New device address (1)
    setup_pkt.wIndex        = 0x0000;
    setup_pkt.wLength       = 0x0000;

    // 2. Configure SETUP TD (Address 0)
    setup_td.link   = virt_to_phys(&status_td);
    setup_td.status = TD_STAT_ACTIVED | (3 << 27); // Active + 3 errors
    setup_td.token  = (7 << 21) | (1 << 26) | (0 << 19) | (0 << 15) | (0 << 8) | USB_PID_SETUP;
    setup_td.buffer = virt_to_phys(&setup_pkt);

    // 3. Configure STATUS TD (Control Write -> OUT packet)
    // DİKKAT: Cihaz setup'ı alır almaz adresini değiştirdiği için status aşaması new_address (1) üzerinden gitmelidir!
    status_td.link   = 1; // Terminate (T = 1)
    status_td.status = TD_STAT_ACTIVED | TD_STAT_IOC | (3 << 27);
    status_td.token  = (0x7FF << 21) | (1 << 26) | (1 << 19) | (0 << 15) | (new_address << 8) | USB_PID_OUT; // <-- Adres alanı new_address (1) yapıldı
    status_td.buffer = 0;

    // 4. Configure Queue Head (QH)
    control_qh.head_link    = 1; // Terminate
    control_qh.element_link = virt_to_phys(&setup_td);

    // 5. Point frame list to QH (Clear all entries first or assign safely)
    uint32_t qh_phys = virt_to_phys(&control_qh) | 0x02; // Bit 1 = Queue Head pointer
    for (int i = 0; i < FRAME_LIST_COUNT; i++) {
        frame_list[i] = qh_phys;
    }
    
    serial_write("UHCI: SET_ADDRESS Queue Head and descriptors dispatched.\n");

    // 6. Wait for transfer completion with a safer timeout loop
    int timeout = 800000; // Süreyi biraz artırıp esnetelim
    while ((status_td.status & TD_STAT_ACTIVED) && (timeout > 0)) {
        timeout--;
        __asm__ volatile("nop");
    }

    if (!(status_td.status & TD_STAT_ACTIVED)) {
        serial_write("UHCI: SET_ADDRESS completed successfully! Device is now at address 1.\n");
    } else {
        serial_write("UHCI: Warning: SET_ADDRESS timed out or failed.\n");
        
        // Status değerini ve hata bayraklarını inceleyelim
        uint32_t st = status_td.status;
        serial_write("  -> SET_ADDRESS: STATUS TD active. Status reg: ");
        
        // Basit bir hex yazdırma yardımı (veya mevcut hex fonksiyonun varsa kullanabilirsin)
        // Stalled bit kontrolü (Bit 26)
        if (st & (1 << 26)) serial_write(" [STALLED] ");
        if (st & (1 << 25)) serial_write(" [Data Buffer Error] ");
        if (st & (1 << 24)) serial_write(" [Babble] ");
        if (st & (1 << 23)) serial_write(" [CRC/Time-out Error] ");
        serial_write("\n");
    }
}

static void uhci_get_device_descriptor(void) {
    serial_write("UHCI: Requesting Device Descriptor (8-byte chunk for Address 0)...\n");

    // 1. Setup Paketi (Get Descriptor: Device Type, Length 8)
    setup_pkt.bmRequestType = 0x80; // Device-to-host, Standard, Recipient: Device
    setup_pkt.bRequest      = 0x06; // GET_DESCRIPTOR
    setup_pkt.wValue        = 0x0100; // Descriptor Type: Device (1), Index: 0
    setup_pkt.wIndex        = 0x0000;
    setup_pkt.wLength       = 0x0008; // İlk aşamada 8 bytes istiyoruz

    // 2. Setup TD (MaxLen = 7 [8 bytes], Toggle = 0 [DATA0], Endpoint = 0, Addr = 0)
    setup_td.link   = virt_to_phys(&in_td);
    setup_td.status = TD_STAT_ACTIVED | (3 << 27);
    setup_td.token  = (7 << 21) | (0 << 19) | (0 << 15) | (0 << 8) | USB_PID_SETUP;
    setup_td.buffer = virt_to_phys(&setup_pkt);

    // 3. In TD (MaxLen = 7 [8 bytes], Toggle = 1 [DATA1], Endpoint = 0, Addr = 0)
    in_td.link   = virt_to_phys(&status_td);
    in_td.status = TD_STAT_ACTIVED | TD_STAT_SPD | (3 << 27);
    in_td.token  = (7 << 21) | (1 << 19) | (0 << 15) | (0 << 8) | USB_PID_IN;
    in_td.buffer = virt_to_phys(device_descriptor);

    // 4. Status TD (MaxLen = 0x7FF [0 bytes], Toggle = 1 [DATA1], Endpoint = 0, Addr = 0)
    status_td.link   = 1; // Terminate
    status_td.status = TD_STAT_ACTIVED | TD_STAT_IOC | (3 << 27);
    status_td.token  = (0x7FF << 21) | (1 << 19) | (0 << 15) | (0 << 8) | USB_PID_OUT;
    status_td.buffer = 0;

    // 5. Queue Head
    control_qh.head_link    = 1;
    control_qh.element_link = virt_to_phys(&setup_td);

    // 6. Frame List'e bağla
    uint32_t qh_phys = virt_to_phys(&control_qh) | 0x02;
    for (int i = 0; i < FRAME_LIST_COUNT; i++) {
        frame_list[i] = qh_phys;
    }

    // 7. Bekleme döngüsü
    int timeout = 2000000;
    while ((status_td.status & TD_STAT_ACTIVED) && (timeout > 0)) {
        timeout--;
        __asm__ volatile("nop");
    }

    if (!(status_td.status & TD_STAT_ACTIVED)) {
        serial_write("UHCI: Device Descriptor (8 bytes) fetched successfully on Address 0!\n");
        
        // Cihazın bMaxPacketSize0 değerini görmek için (genellikle 7. bayttır):
        uint8_t max_packet_size = device_descriptor[7];
        serial_write("  -> MaxPacketSize0: ");
        // Eğer basit bir sayı yazdırma fonksiyonun yoksa en azından başarılı olduğunu anlıyoruz
    } else {
        serial_write("UHCI: Warning: Get Device Descriptor on Address 0 timed out or failed.\n");
        uint32_t in_st = in_td.status;
        serial_write("  -> IN_TD Status: ");
        if (in_st & (1 << 26)) serial_write("[STALLED] ");
        if (in_st & (1 << 23)) serial_write("[Timeout] ");
        serial_write("\n");
    }
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
                
                // Gecikmeyi biraz uzatalım ki cihaz tamamen kendine gelsin
                for (volatile int d = 0; d < 800000; d++) {
                    __asm__ volatile("nop");
                }
                
                uhci_get_device_descriptor();
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

    // Search for UHCI controller on the PCI bus using the helper function
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
    // Provide physical address of frame_list to the controller
    uhci_outl(uhci_io_base + UHCI_FRBASEADD, virt_to_phys(frame_list));
    uhci_outw(uhci_io_base + UHCI_FRNUM, 0);

    // 3. Start the Controller (Run/Stop)
    uhci_outw(uhci_io_base + UHCI_USBCMD, USBCMD_RS | (1 << 7));

    serial_write("UHCI: Controller successfully started (Running).\n");

    for (volatile int i = 0; i < 500000; i++);

    // Check port statuses
    uhci_check_ports();
}