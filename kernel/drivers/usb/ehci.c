#include <kernel/drivers/usb/ehci.h>
#include <kernel/drivers/pci.h>
#include <kernel/serial.h>
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
#define EHCI_PORTSC      0x44

#define EHCI_PORT_CONNECT (1 << 0)
#define EHCI_PORT_CONNECT_CHANGE (1 << 1)
#define EHCI_PORT_ENABLED (1 << 2)
#define EHCI_PORT_ENABLE_CHANGE (1 << 3)
#define EHCI_PORT_RESET (1 << 8)
#define EHCI_PORT_OWNER (1 << 13)
#define EHCI_PORT_POWER (1 << 12)

static volatile uint8_t* ehci_mmio_base = 0;
static volatile uint8_t* ehci_op_base = 0;

// EHCI Queue Head (QH) yapısı (32-bit hizalı olmalı)
typedef struct {
    uint32_t horizontal_link;
    uint32_t endpoint_chars;
    uint32_t endpoint_caps;
    uint32_t current_dtd;
    uint32_t next_dtd;
    uint32_t alt_next_dtd;
    uint32_t token;
    uint32_t buffer_pointers[5];
} __attribute__((packed)) ehci_qh_t;

// EHCI Queue Transfer Descriptor (qTD) yapısı (32-bit hizalı olmalı)
typedef struct {
    uint32_t next_dtd;             // Sonraki qTD işaretçisi (Terminator bit dahil)
    uint32_t alt_next_dtd;         // Alternatif sonraki qTD işaretçisi
    uint32_t token;                // Durum, PID, bayt sayısı, toggle vb.
    uint32_t buffer_pointers[5];   // Veri bellek adresleri (Buffer Page 0-4)
} __attribute__((packed)) ehci_qtd_t;

static ehci_qh_t async_head __attribute__((aligned(32)));
static ehci_qh_t control_qh __attribute__((aligned(32)));
static ehci_qtd_t set_address_setup_qtd __attribute__((aligned(32)));
static ehci_qtd_t set_address_status_qtd __attribute__((aligned(32)));
static uint8_t set_address_setup_packet[8] __attribute__((aligned(16)));
static uint8_t ehci_device_addressed = 0;

static bool ehci_wait_microframes(uint32_t microframes) {
    volatile uint32_t* frindex = (volatile uint32_t*)(ehci_op_base + EHCI_FRINDEX);
    uint32_t start = *frindex & 0x3FFF;
    uint32_t timeout = 50000000;

    while (timeout--) {
        uint32_t current = *frindex & 0x3FFF;
        if (((current - start) & 0x3FFF) >= microframes) return true;
    }
    return false;
}

// qTD Token alanını oluşturan yardımcı makro/fonksiyon
#define EHCI_QTD_TOGGLE_0      (0 << 31)
#define EHCI_QTD_TOGGLE_1      (1 << 31)
#define EHCI_QTD_LENGTH(len)   ((len) << 16)
#define EHCI_QTD_IOC           (1 << 15) // Interrupt On Complete
#define EHCI_QTD_STATUS_ACTIVE (1 << 7)
#define EHCI_QTD_PID_SETUP     (2 << 8)
#define EHCI_QTD_PID_IN        (1 << 8)
#define EHCI_QTD_PID_OUT       (0 << 8)

// USB Set Address Komutunu Gönderme Fonksiyonu
static bool ehci_set_address(uint8_t target_address, uint8_t port_speed) {
    serial_write("EHCI: Sending SET_ADDRESS to device...\n");

    if (port_speed != 2) {
        serial_write("EHCI: SET_ADDRESS currently requires a high-speed device.\n");
        return false;
    }

    set_address_setup_packet[0] = 0x00;
    set_address_setup_packet[1] = 0x05;
    set_address_setup_packet[2] = target_address;
    set_address_setup_packet[3] = 0;
    set_address_setup_packet[4] = 0;
    set_address_setup_packet[5] = 0;
    set_address_setup_packet[6] = 0;
    set_address_setup_packet[7] = 0;

    set_address_setup_qtd.next_dtd = (uint32_t)&set_address_status_qtd;
    set_address_setup_qtd.alt_next_dtd = 1;
    set_address_setup_qtd.token = EHCI_QTD_STATUS_ACTIVE
        | EHCI_QTD_LENGTH(8) | EHCI_QTD_PID_SETUP | (3 << 10);
    set_address_setup_qtd.buffer_pointers[0] = (uint32_t)set_address_setup_packet;

    set_address_status_qtd.next_dtd = 1;
    set_address_status_qtd.alt_next_dtd = 1;
    set_address_status_qtd.token = EHCI_QTD_STATUS_ACTIVE | EHCI_QTD_IOC
        | EHCI_QTD_PID_IN | (3 << 10) | EHCI_QTD_TOGGLE_1;

    control_qh.current_dtd = 0;
    control_qh.next_dtd = (uint32_t)&set_address_setup_qtd;
    control_qh.alt_next_dtd = 1;
    control_qh.token = 0;

    __asm__ volatile("" ::: "memory");

    uint32_t timeout = 10000000;
    while ((set_address_status_qtd.token & EHCI_QTD_STATUS_ACTIVE) && --timeout) {
        __asm__ volatile("pause");
    }

    if (!timeout || (set_address_status_qtd.token & 0x7F)) {
        serial_write("EHCI Error: SET_ADDRESS failed, qTD token=0x");
        serial_write_hex8(set_address_status_qtd.token >> 24);
        serial_write_hex8(set_address_status_qtd.token >> 16);
        serial_write_hex8(set_address_status_qtd.token >> 8);
        serial_write_hex8(set_address_status_qtd.token);
        serial_write("\n");
        return false;
    }

    control_qh.endpoint_chars = target_address | (port_speed << 12) | (64 << 16);
    for (volatile int delay = 0; delay < 20000; delay++);
    ehci_device_addressed = 1;
    serial_write("EHCI: SET_ADDRESS completed successfully!\n");
    return true;
}

void ehci_check_ports(void) {
    serial_write("EHCI: Powering and checking downstream ports...\n");

    uint32_t hcsparams = *((volatile uint32_t*)(ehci_mmio_base + EHCI_HCSPARAMS));
    uint8_t n_ports = hcsparams & 0x0F;
    
    serial_write("EHCI: Total downstream ports found: ");
    serial_write_dec(n_ports);
    serial_write("\n");

    for (uint8_t i = 0; i < n_ports; i++) {
        volatile uint32_t* portsc = (volatile uint32_t*)(ehci_op_base + EHCI_PORTSC + (i * 4));
        uint32_t val = *portsc;
        *portsc = (val & EHCI_PORT_OWNER) | EHCI_PORT_POWER;
    }

    // Port gücünün ve sahipliğin çipset içinde oturması için bekleme
    for (volatile int d = 0; d < 200000; d++);

    // Portları tekrar oku ve durumlarını kontrol et
    for (uint8_t i = 0; i < n_ports; i++) {
        volatile uint32_t* portsc = (volatile uint32_t*)(ehci_op_base + EHCI_PORTSC + (i * 4));
        uint32_t val = *portsc;

        serial_write("EHCI Port ");
        serial_write_dec(i + 1);
        if (val & EHCI_PORT_CONNECT) {
            serial_write(": device connected");

            if (val & EHCI_PORT_OWNER) {
                serial_write(", owned by companion controller\n");
                continue;
            }

            if (val & (EHCI_PORT_CONNECT_CHANGE | EHCI_PORT_ENABLE_CHANGE)) {
                *portsc = EHCI_PORT_POWER
                    | (val & EHCI_PORT_OWNER)
                    | (val & EHCI_PORT_CONNECT_CHANGE)
                    | (val & EHCI_PORT_ENABLE_CHANGE);
            }

            // EHCI requires at least 50 ms of port reset before software clears PR.
            *portsc = EHCI_PORT_POWER | EHCI_PORT_RESET;
            if (!ehci_wait_microframes(400)) {
                serial_write(", frame index stalled during port reset");
            }
            *portsc = EHCI_PORT_POWER;
            ehci_wait_microframes(16);

            uint32_t timeout = 100000;
            while (!(*portsc & (EHCI_PORT_ENABLED | EHCI_PORT_OWNER)) && --timeout);
            val = *portsc;
            if (val & EHCI_PORT_OWNER) {
                serial_write(", handed to companion controller\n");
                continue;
            }

            if (timeout) {
                serial_write(", port enabled\n");
                if (!ehci_device_addressed) {
                    uint8_t port_speed = (uint8_t)((val >> 26) & 0x03);
                    if (port_speed == 2) {
                        if (ehci_set_address(1, port_speed)) {
                            serial_write("EHCI: Device is ready for descriptor requests.\n");
                        }
                    } else {
                        *portsc = EHCI_PORT_POWER | EHCI_PORT_OWNER;
                        serial_write("EHCI: Full/low-speed device handed to companion controller.\n");
                    }
                }
            } else {
                serial_write(", port reset/enable failed (PORTSC=0x");
                val = *portsc;
                serial_write_hex8(val >> 24);
                serial_write_hex8(val >> 16);
                serial_write_hex8(val >> 8);
                serial_write_hex8(val);
                serial_write(")\n");
            }
        } else {
            serial_write(": empty\n");
        }
    }
}

// Asenkron Kuyruk (Async Schedule) Altyapısını Başlatma
static void ehci_init_async_schedule(void) {
    serial_write("EHCI: Initializing Asynchronous Schedule...\n");

    uint8_t* head_bytes = (uint8_t*)&async_head;
    uint8_t* qh_bytes = (uint8_t*)&control_qh;
    uint8_t* setup_bytes = (uint8_t*)&set_address_setup_qtd;
    uint8_t* status_bytes = (uint8_t*)&set_address_status_qtd;
    for (uint32_t i = 0; i < sizeof(async_head); i++) head_bytes[i] = 0;
    for (uint32_t i = 0; i < sizeof(control_qh); i++) qh_bytes[i] = 0;
    for (uint32_t i = 0; i < sizeof(set_address_setup_qtd); i++) setup_bytes[i] = 0;
    for (uint32_t i = 0; i < sizeof(set_address_status_qtd); i++) status_bytes[i] = 0;

    uint32_t async_head_phys = (uint32_t)&async_head;
    uint32_t control_qh_phys = (uint32_t)&control_qh;
    async_head.horizontal_link = control_qh_phys | (1 << 1);
    async_head.endpoint_chars = 1 << 15;
    control_qh.horizontal_link = async_head_phys | (1 << 1);
    control_qh.endpoint_chars = (2 << 12) | (64 << 16);

    // ASYNCLISTADDR register'ına QH adresini yaz
    *((volatile uint32_t*)(ehci_op_base + EHCI_ASYNCLISTADDR)) = async_head_phys;

    // USBCMD içerisinden Asynchronous Schedule Enable (Bit 5) bitini set et
    uint32_t usbcmd = *((volatile uint32_t*)(ehci_op_base + EHCI_USBCMD));
    usbcmd |= (1 << 5); // Asynchronous Schedule Enable
    *((volatile uint32_t*)(ehci_op_base + EHCI_USBCMD)) = usbcmd;

    serial_write("EHCI: Asynchronous Schedule enabled successfully.\n");
}

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

    // 2. Route Port to EHCI (Config Flag = 1 yazarak portları EHCI'a devret)
    *((volatile uint32_t*)(ehci_op_base + EHCI_CONFIGFLAG)) = 1;

    // Config Flag değerini okuyarak QEMU tarafında set edilip edilmediğini doğrula
    uint32_t cfg_val = *((volatile uint32_t*)(ehci_op_base + EHCI_CONFIGFLAG));
    serial_write("EHCI: CONFIGFLAG Register Value: 0x");
    serial_write_hex8(cfg_val >> 24);
    serial_write_hex8(cfg_val >> 16);
    serial_write_hex8(cfg_val >> 8);
    serial_write_hex8(cfg_val);
    serial_write("\n");

    // Port routing sinyalinin çipset içinde oturması için bekleme
    for (volatile int d = 0; d < 100000; d++);

    // Asenkron kuyruk altyapısını kur
    ehci_init_async_schedule();

    // 3. Kontrolcüyü yeniden çalıştır (Run/Stop = 1)
    usbcmd = *((volatile uint32_t*)(ehci_op_base + EHCI_USBCMD));
    usbcmd |= 1; // RS bitini set et
    *((volatile uint32_t*)(ehci_op_base + EHCI_USBCMD)) = usbcmd;

    // Kontrolcünün koşmaya başlaması için bekle
    for (volatile int d = 0; d < 100000; d++);

    serial_write("EHCI: Controller started and running.\n");

    // 4. Kontrolcü çalıştıktan sonra portlara güç ver ve durumları tara
    ehci_check_ports();

    return true;
}