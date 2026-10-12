#include <kernel/drivers/usb/ehci.h>
#include <kernel/drivers/pci.h>
#include <kernel/drivers/input/mouse_usb.h>
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
#define EHCI_CTRLDSSEG   0x10
#define EHCI_PERIODICLIST 0x14
#define EHCI_ASYNCLISTADDR 0x18
#define EHCI_CONFIGFLAG  0x40
#define EHCI_PORTSC      0x44

#define EHCI_USBCMD_RUN (1 << 0)
#define EHCI_USBCMD_RESET (1 << 1)
#define EHCI_USBSTS_HOST_ERROR (1 << 4)
#define EHCI_USBSTS_HALTED (1 << 12)
#define EHCI_USBSTS_W1C_MASK 0x3F

#define EHCI_PORT_CONNECT (1 << 0)
#define EHCI_PORT_CONNECT_CHANGE (1 << 1)
#define EHCI_PORT_ENABLED (1 << 2)
#define EHCI_PORT_ENABLE_CHANGE (1 << 3)
#define EHCI_PORT_RESET (1 << 8)
#define EHCI_PORT_OWNER (1 << 13)
#define EHCI_PORT_POWER (1 << 12)

#define EHCI_FRAME_LIST_COUNT 1024
#define USB_HUB_CLASS 0x09
#define USB_HID_CLASS 0x03
#define USB_DESC_CONFIGURATION 0x02
#define USB_DESC_HUB 0x29
#define USB_REQ_GET_DESCRIPTOR 0x06
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_SET_FEATURE 0x03
#define USB_REQ_CLEAR_FEATURE 0x01
#define USB_HUB_FEATURE_PORT_RESET 4
#define USB_HUB_FEATURE_PORT_POWER 8
#define USB_HUB_FEATURE_C_PORT_RESET 20
#define EHCI_USBSTS_PERIODIC_STATUS (1 << 14)

#define PIT_CHANNEL_2_PORT 0x42
#define PIT_COMMAND_PORT   0x43
#define PPI_SYSTEM_PORT    0x61
#define PIT_BASE_FREQ      1193180

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
static ehci_qtd_t control_setup_qtd __attribute__((aligned(32)));
static ehci_qtd_t control_data_qtd __attribute__((aligned(32)));
static ehci_qtd_t control_status_qtd __attribute__((aligned(32)));
static uint8_t control_setup_packet[8] __attribute__((aligned(16)));
static uint8_t device_descriptor[18] __attribute__((aligned(16)));
static uint8_t config_descriptor[128] __attribute__((aligned(16)));
static uint8_t hub_descriptor[16] __attribute__((aligned(16)));
static uint8_t ehci_device_addressed = 0;
static uint8_t ehci_hub_address = 0;
static uint8_t ehci_hub_port_count = 0;
static uint8_t ehci_mouse_ready = 0;
static uint8_t ehci_mouse_error_logged = 0;
static uint8_t ehci_mouse_address = 0;
static uint8_t ehci_mouse_endpoint = 0;
static uint8_t ehci_mouse_speed = 0;
static uint8_t ehci_mouse_interval = 1;
static uint8_t ehci_mouse_data_toggle = 0;
static uint8_t ehci_mouse_tt_port = 0;
static uint16_t ehci_mouse_max_packet = 0;
static ehci_qh_t mouse_interrupt_qh __attribute__((aligned(32)));
static ehci_qtd_t mouse_interrupt_qtd __attribute__((aligned(32)));
static uint8_t mouse_interrupt_buffer[8] __attribute__((aligned(16)));
static uint32_t periodic_frame_list[EHCI_FRAME_LIST_COUNT] __attribute__((aligned(4096)));

static bool ehci_delay_ms(uint16_t milliseconds) {
    uint32_t count = (PIT_BASE_FREQ * milliseconds) / 1000;
    uint8_t system_port = inb(PPI_SYSTEM_PORT);

    outb(PPI_SYSTEM_PORT, system_port & (uint8_t)~0x03);
    outb(PIT_COMMAND_PORT, 0xB0);
    outb(PIT_CHANNEL_2_PORT, (uint8_t)count);
    outb(PIT_CHANNEL_2_PORT, (uint8_t)(count >> 8));
    outb(PPI_SYSTEM_PORT, (system_port & (uint8_t)~0x02) | 0x01);

    uint32_t timeout = 10000000;
    while (!(inb(PPI_SYSTEM_PORT) & 0x20) && --timeout) {
        __asm__ volatile("pause");
    }

    outb(PPI_SYSTEM_PORT, system_port);
    return timeout != 0;
}

static bool ehci_handoff_to_os(pci_device_t* dev, uint32_t hccparams) {
    uint8_t capability_offset = (uint8_t)((hccparams >> 8) & 0xFF);
    if (capability_offset == 0) {
        serial_write("EHCI: No legacy support capability reported.\n");
        return true;
    }

    for (uint8_t count = 0; count < 48 && capability_offset >= 0x40;
         count++) {
        if (capability_offset & 0x03) {
            serial_write("EHCI: Invalid extended capability alignment.\n");
            return false;
        }

        uint32_t capability = pci_read_config32(dev->bus, dev->slot, dev->func,
                                                capability_offset);
        uint8_t capability_id = (uint8_t)(capability & 0xFF);
        uint8_t next_offset = (uint8_t)((capability >> 8) & 0xFF);

        if (capability_id == 0x01) {
            const uint32_t bios_owned = 1u << 16;
            const uint32_t os_owned = 1u << 24;
            serial_write("EHCI: Requesting OS ownership of USB controller...\n");
            pci_write_config32(dev->bus, dev->slot, dev->func, capability_offset,
                               capability | os_owned);

            uint32_t status = capability;
            for (uint8_t attempt = 0; attempt < 100; attempt++) {
                status = pci_read_config32(dev->bus, dev->slot, dev->func,
                                           capability_offset);
                if (!(status & bios_owned)) break;
                ehci_delay_ms(10);
            }

            if (status & bios_owned) {
                serial_write("EHCI Error: BIOS did not release controller ownership.\n");
                return false;
            }

            serial_write("EHCI: BIOS ownership released; OS owns controller.\n");
            return true;
        }

        if (next_offset == 0 || next_offset <= capability_offset) break;
        capability_offset = next_offset;
    }

    serial_write("EHCI: Legacy support capability not found.\n");
    return true;
}

static void ehci_log_portsc(uint8_t port_number, const char* stage,
                            uint32_t value) {
    serial_write("EHCI Port ");
    serial_write_dec(port_number);
    serial_write(" PORTSC ");
    serial_write(stage);
    serial_write("=0x");
    serial_write_hex8(value >> 24);
    serial_write_hex8(value >> 16);
    serial_write_hex8(value >> 8);
    serial_write_hex8(value);
    serial_write("\n");
}

static void ehci_log_controller_state(const char* stage) {
    uint32_t usbcmd = *((volatile uint32_t*)(ehci_op_base + EHCI_USBCMD));
    uint32_t usbsts = *((volatile uint32_t*)(ehci_op_base + EHCI_USBSTS));

    serial_write("EHCI Controller ");
    serial_write(stage);
    serial_write(" USBCMD=0x");
    serial_write_hex8(usbcmd >> 24);
    serial_write_hex8(usbcmd >> 16);
    serial_write_hex8(usbcmd >> 8);
    serial_write_hex8(usbcmd);
    serial_write(" USBSTS=0x");
    serial_write_hex8(usbsts >> 24);
    serial_write_hex8(usbsts >> 16);
    serial_write_hex8(usbsts >> 8);
    serial_write_hex8(usbsts);
    serial_write("\n");
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

static void ehci_set_qtd_buffer(ehci_qtd_t* qtd, const uint8_t* buffer) {
    uint32_t address = (uint32_t)buffer;
    qtd->buffer_pointers[0] = address;
    for (uint8_t page = 1; page < 5; page++) {
        qtd->buffer_pointers[page] = (address & 0xFFFFF000)
            + ((uint32_t)page * 0x1000);
    }
}

static bool ehci_control_transfer(uint8_t address, uint8_t speed,
                                  uint16_t max_packet_size,
                                  uint8_t tt_hub_address, uint8_t tt_port,
                                  const uint8_t setup[8], uint8_t* data,
                                  uint16_t length) {
    if (speed > 2 || max_packet_size == 0 || length > 4096
        || (speed != 2 && tt_hub_address == 0)) return false;

    for (uint32_t i = 0; i < sizeof(control_setup_qtd); i++) {
        ((uint8_t*)&control_setup_qtd)[i] = 0;
        ((uint8_t*)&control_data_qtd)[i] = 0;
        ((uint8_t*)&control_status_qtd)[i] = 0;
    }
    for (uint8_t i = 0; i < 8; i++) control_setup_packet[i] = setup[i];

    ehci_qtd_t* first_data_stage = length ? &control_data_qtd : &control_status_qtd;
    control_setup_qtd.next_dtd = (uint32_t)first_data_stage;
    control_setup_qtd.alt_next_dtd = 1;
    control_setup_qtd.token = EHCI_QTD_STATUS_ACTIVE | EHCI_QTD_LENGTH(8)
        | EHCI_QTD_PID_SETUP | (3 << 10);
    ehci_set_qtd_buffer(&control_setup_qtd, control_setup_packet);

    if (length) {
        uint32_t data_pid = (setup[0] & 0x80) ? EHCI_QTD_PID_IN : EHCI_QTD_PID_OUT;
        control_data_qtd.next_dtd = (uint32_t)&control_status_qtd;
        control_data_qtd.alt_next_dtd = (setup[0] & 0x80)
            ? (uint32_t)&control_status_qtd : 1;
        control_data_qtd.token = EHCI_QTD_STATUS_ACTIVE | EHCI_QTD_LENGTH(length)
            | data_pid | (3 << 10) | EHCI_QTD_TOGGLE_1;
        ehci_set_qtd_buffer(&control_data_qtd, data);
    }

    control_status_qtd.next_dtd = 1;
    control_status_qtd.alt_next_dtd = 1;
    control_status_qtd.token = EHCI_QTD_STATUS_ACTIVE | EHCI_QTD_IOC
        | ((setup[0] & 0x80) ? EHCI_QTD_PID_OUT : EHCI_QTD_PID_IN)
        | (3 << 10) | EHCI_QTD_TOGGLE_1;

    control_qh.endpoint_chars = address | (speed << 12)
        | (max_packet_size << 16) | (1 << 27);
    control_qh.endpoint_caps = speed == 2 ? 0
        : 0x01u | (0x1Cu << 8) | ((uint32_t)tt_hub_address << 16)
            | ((uint32_t)tt_port << 23);
    control_qh.current_dtd = 0;
    control_qh.next_dtd = (uint32_t)&control_setup_qtd;
    control_qh.alt_next_dtd = 1;
    control_qh.token = 0;

    __asm__ volatile("" ::: "memory");

    uint32_t timeout = 10000000;
    while ((control_status_qtd.token & EHCI_QTD_STATUS_ACTIVE) && --timeout) {
        __asm__ volatile("pause");
    }

    if (!timeout || (control_status_qtd.token & 0x7F)) {
        serial_write("EHCI Error: Control transfer failed, qTD token=0x");
        serial_write_hex8(control_status_qtd.token >> 24);
        serial_write_hex8(control_status_qtd.token >> 16);
        serial_write_hex8(control_status_qtd.token >> 8);
        serial_write_hex8(control_status_qtd.token);
        serial_write("\n");
        return false;
    }
    return true;
}

static bool ehci_set_address(uint8_t target_address, uint8_t speed,
                             uint8_t max_packet_size, uint8_t tt_hub_address,
                             uint8_t tt_port) {
    uint8_t setup[8] = { 0x00, 0x05, target_address, 0, 0, 0, 0, 0 };
    if (!ehci_control_transfer(0, speed, max_packet_size, tt_hub_address,
                               tt_port, setup, 0, 0)) return false;

    for (volatile int delay = 0; delay < 20000; delay++);
    ehci_device_addressed = 1;
    serial_write("EHCI: SET_ADDRESS completed successfully.\n");
    return true;
}

static bool ehci_get_device_descriptor(uint8_t address, uint8_t speed,
                                       uint8_t max_packet_size,
                                       uint8_t tt_hub_address, uint8_t tt_port,
                                       uint16_t requested_length) {
    uint8_t setup[8] = { 0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x12, 0x00 };
    for (uint8_t i = 0; i < sizeof(device_descriptor); i++) device_descriptor[i] = 0;
    if (requested_length > sizeof(device_descriptor)) return false;
    setup[6] = (uint8_t)requested_length;
    setup[7] = (uint8_t)(requested_length >> 8);

    if (!ehci_control_transfer(address, speed, max_packet_size, tt_hub_address,
                               tt_port, setup, device_descriptor, requested_length)) {
        return false;
    }

    if (requested_length < sizeof(device_descriptor)) return true;

    uint16_t vendor = (uint16_t)device_descriptor[8]
        | ((uint16_t)device_descriptor[9] << 8);
    uint16_t product = (uint16_t)device_descriptor[10]
        | ((uint16_t)device_descriptor[11] << 8);
    serial_write("EHCI: Device descriptor received. Class=0x");
    serial_write_hex8(device_descriptor[4]);
    serial_write(" VID=0x");
    serial_write_hex16(vendor);
    serial_write(" PID=0x");
    serial_write_hex16(product);
    serial_write("\n");

    if (device_descriptor[4] == 0x09) {
        serial_write("EHCI: High-speed hub detected; next enumerate its TT ports.\n");
    }
    return true;
}

static bool ehci_get_configuration_descriptor(uint8_t address, uint8_t speed,
                                              uint8_t max_packet_size,
                                              uint8_t tt_hub_address,
                                              uint8_t tt_port) {
    uint8_t setup[8] = { 0x80, USB_REQ_GET_DESCRIPTOR, 0x00,
                         USB_DESC_CONFIGURATION, 0, 0, 9, 0 };
    if (!ehci_control_transfer(address, speed, max_packet_size,
                               tt_hub_address, tt_port, setup,
                               config_descriptor, 9)) return false;

    uint16_t total_length = (uint16_t)config_descriptor[2]
        | ((uint16_t)config_descriptor[3] << 8);
    if (total_length < 9 || total_length > sizeof(config_descriptor)) {
        serial_write("EHCI: Invalid configuration descriptor length.\n");
        return false;
    }

    setup[6] = (uint8_t)total_length;
    setup[7] = (uint8_t)(total_length >> 8);
    return ehci_control_transfer(address, speed, max_packet_size,
                                 tt_hub_address, tt_port, setup,
                                 config_descriptor, total_length);
}

static bool ehci_set_configuration(uint8_t address, uint8_t speed,
                                  uint16_t max_packet_size,
                                  uint8_t tt_hub_address, uint8_t tt_port,
                                  uint8_t configuration) {
    uint8_t setup[8] = { 0x00, USB_REQ_SET_CONFIGURATION,
                         configuration, 0, 0, 0, 0, 0 };
    return ehci_control_transfer(address, speed, max_packet_size,
                                 tt_hub_address, tt_port, setup, 0, 0);
}

static bool ehci_hub_set_port_feature(uint8_t hub_address, uint8_t port,
                                      uint16_t feature, uint8_t set) {
    uint8_t setup[8] = {
        0x23,
        set ? USB_REQ_SET_FEATURE : USB_REQ_CLEAR_FEATURE,
        (uint8_t)feature, (uint8_t)(feature >> 8),
        port, 0, 0, 0
    };
    return ehci_control_transfer(hub_address, 2, 64, 0, 0, setup, 0, 0);
}

static bool ehci_hub_get_port_status(uint8_t hub_address, uint8_t port,
                                     uint8_t status[4]) {
    uint8_t setup[8] = { 0xA3, 0x00, 0, 0, port, 0, 4, 0 };
    return ehci_control_transfer(hub_address, 2, 64, 0, 0, setup, status, 4);
}

static bool ehci_start_mouse_interrupt(uint8_t address, uint8_t speed,
                                       uint8_t endpoint, uint16_t max_packet,
                                       uint8_t interval, uint8_t tt_port) {
    if (max_packet < 3 || max_packet > sizeof(mouse_interrupt_buffer)) {
        serial_write("EHCI: Unsupported interrupt mouse packet size.\n");
        return false;
    }
    if (interval == 0) interval = 1;
    ehci_mouse_address = address;
    ehci_mouse_speed = speed;
    ehci_mouse_endpoint = endpoint;
    ehci_mouse_max_packet = max_packet;
    ehci_mouse_interval = interval;
    ehci_mouse_tt_port = speed == 2 ? 0 : tt_port;

    mouse_interrupt_qh.horizontal_link = 1;
    mouse_interrupt_qh.endpoint_chars = address | ((uint32_t)(endpoint & 0x0F) << 8)
        | ((uint32_t)speed << 12) | (1 << 14) | ((uint32_t)max_packet << 16);
    mouse_interrupt_qh.endpoint_caps = speed == 2 ? (1u << 30)
        : 0x01u | (0x1Cu << 8) | ((uint32_t)ehci_hub_address << 16)
            | ((uint32_t)tt_port << 23) | (1u << 30);
    mouse_interrupt_qh.current_dtd = 0;
    mouse_interrupt_qh.next_dtd = (uint32_t)&mouse_interrupt_qtd;
    mouse_interrupt_qh.alt_next_dtd = 1;
    mouse_interrupt_qh.token = 0;

    mouse_interrupt_qtd.next_dtd = 1;
    mouse_interrupt_qtd.alt_next_dtd = 1;
    mouse_interrupt_qtd.token = EHCI_QTD_STATUS_ACTIVE | EHCI_QTD_IOC
        | EHCI_QTD_LENGTH(max_packet) | EHCI_QTD_PID_IN | (3 << 10)
        | (ehci_mouse_data_toggle ? EHCI_QTD_TOGGLE_1 : EHCI_QTD_TOGGLE_0);
    ehci_set_qtd_buffer(&mouse_interrupt_qtd, mouse_interrupt_buffer);

    uint32_t qh_link = (uint32_t)&mouse_interrupt_qh | (1 << 1);
    for (uint32_t frame = 0; frame < EHCI_FRAME_LIST_COUNT; frame++) {
        periodic_frame_list[frame] = (frame % interval == 0) ? qh_link : 1;
    }
    *((volatile uint32_t*)(ehci_op_base + EHCI_PERIODICLIST))
        = (uint32_t)periodic_frame_list;
    __asm__ volatile("" ::: "memory");

    uint32_t usbcmd = *((volatile uint32_t*)(ehci_op_base + EHCI_USBCMD));
    usbcmd |= 1 << 4;
    *((volatile uint32_t*)(ehci_op_base + EHCI_USBCMD)) = usbcmd;

    uint32_t timeout = 1000000;
    while (!(*((volatile uint32_t*)(ehci_op_base + EHCI_USBSTS))
             & EHCI_USBSTS_PERIODIC_STATUS) && --timeout);
    if (!timeout) {
        serial_write("EHCI Error: Periodic schedule did not start.\n");
        return false;
    }

    ehci_mouse_ready = 1;
    serial_write("EHCI: Mouse interrupt polling started through hub TT.\n");
    return true;
}

static bool ehci_enumerate_hub_mouse(uint8_t port) {
    uint8_t status[4] = { 0, 0, 0, 0 };
    if (!ehci_hub_get_port_status(ehci_hub_address, port, status)) return false;

    uint16_t port_status = (uint16_t)status[0] | ((uint16_t)status[1] << 8);
    uint16_t port_change = (uint16_t)status[2] | ((uint16_t)status[3] << 8);
    if (!(port_status & 1)) return false;
    if (port_change & 1) ehci_hub_set_port_feature(ehci_hub_address, port, 16, 0);

    if (!ehci_hub_set_port_feature(ehci_hub_address, port,
                                   USB_HUB_FEATURE_PORT_RESET, 1)) return false;
    ehci_delay_ms(50);
    ehci_hub_set_port_feature(ehci_hub_address, port,
                              USB_HUB_FEATURE_C_PORT_RESET, 0);
    if (!ehci_hub_get_port_status(ehci_hub_address, port, status)) return false;

    port_status = (uint16_t)status[0] | ((uint16_t)status[1] << 8);
    if (!(port_status & (1 << 1))) {
        serial_write("EHCI: Hub downstream port did not enable.\n");
        return false;
    }

    uint8_t speed = (port_status & (1 << 10)) ? 2
        : ((port_status & (1 << 9)) ? 1 : 0);
    uint8_t tt_hub = speed == 2 ? 0 : ehci_hub_address;
    uint8_t tt_port = speed == 2 ? 0 : port;
    uint8_t setup[8] = { 0x80, USB_REQ_GET_DESCRIPTOR, 0, 1, 0, 0, 8, 0 };
    if (!ehci_control_transfer(0, speed, 8, tt_hub, tt_port,
                               setup, device_descriptor, 8)) {
        serial_write("EHCI: Failed to read hub child descriptor header.\n");
        return false;
    }

    uint8_t max_packet_size = device_descriptor[7];
    if (max_packet_size == 0 || max_packet_size > 64) return false;
    if (!ehci_set_address(2, speed, max_packet_size, tt_hub, tt_port)) return false;
    ehci_delay_ms(2);
    if (!ehci_get_device_descriptor(2, speed, max_packet_size,
                                    tt_hub, tt_port, 18)) return false;
    if (!ehci_get_configuration_descriptor(2, speed, max_packet_size,
                                           tt_hub, tt_port)) return false;

    uint8_t hid_found = 0;
    uint8_t endpoint = 0;
    uint8_t interval = 1;
    uint16_t endpoint_packet_size = 0;
    uint16_t total_length = (uint16_t)config_descriptor[2]
        | ((uint16_t)config_descriptor[3] << 8);
    for (uint16_t offset = 0; offset + 2 <= total_length;) {
        uint8_t desc_length = config_descriptor[offset];
        uint8_t desc_type = config_descriptor[offset + 1];
        if (desc_length < 2 || offset + desc_length > total_length) break;

        if (desc_type == 4 && desc_length >= 9
            && config_descriptor[offset + 5] == USB_HID_CLASS) {
            hid_found = 1;
            usb_mouse_set_absolute_mode(!(config_descriptor[offset + 6] == 1
                                          && config_descriptor[offset + 7] == 2));
        } else if (hid_found && desc_type == 5 && desc_length >= 7
                   && (config_descriptor[offset + 2] & 0x80)
                   && (config_descriptor[offset + 3] & 3) == 3) {
            endpoint = config_descriptor[offset + 2] & 0x0F;
            endpoint_packet_size = (uint16_t)config_descriptor[offset + 4]
                | ((uint16_t)config_descriptor[offset + 5] << 8);
            interval = config_descriptor[offset + 6];
            break;
        }
        offset += desc_length;
    }

    if (!hid_found || endpoint == 0
        || !ehci_set_configuration(2, speed, max_packet_size, tt_hub, tt_port,
                                   config_descriptor[5])) {
        serial_write("EHCI: Hub child is not a configurable HID mouse.\n");
        return false;
    }

    serial_write("EHCI: HID interrupt endpoint found: ");
    serial_write_dec(endpoint);
    serial_write(" MaxPacket: ");
    serial_write_dec(endpoint_packet_size & 0x7FF);
    serial_write(" Interval: ");
    serial_write_dec(interval);
    serial_write("\n");
    return ehci_start_mouse_interrupt(2, speed, endpoint,
                                      endpoint_packet_size & 0x7FF,
                                      interval, tt_port);
}

static bool ehci_enumerate_hub(uint8_t address) {
    if (!ehci_get_configuration_descriptor(address, 2, 64, 0, 0)) {
        serial_write("EHCI: Failed to read hub configuration descriptor.\n");
        return false;
    }
    if (!ehci_set_configuration(address, 2, 64, 0, 0, config_descriptor[5])) {
        serial_write("EHCI: Failed to configure hub.\n");
        return false;
    }

    uint8_t setup[8] = { 0xA0, USB_REQ_GET_DESCRIPTOR, 0x00,
                         USB_DESC_HUB, 0, 0, sizeof(hub_descriptor), 0 };
    if (!ehci_control_transfer(address, 2, 64, 0, 0, setup,
                               hub_descriptor, sizeof(hub_descriptor))) {
        serial_write("EHCI: Failed to read hub descriptor.\n");
        return false;
    }

    ehci_hub_address = address;
    ehci_hub_port_count = hub_descriptor[2];
    if (ehci_hub_port_count == 0 || ehci_hub_port_count > 15) {
        serial_write("EHCI: Hub reports an unsupported port count.\n");
        return false;
    }

    serial_write("EHCI: Configured high-speed hub at address ");
    serial_write_dec(address);
    serial_write(" with ");
    serial_write_dec(ehci_hub_port_count);
    serial_write(" ports.\n");

    for (uint8_t port = 1; port <= ehci_hub_port_count; port++) {
        if (!ehci_hub_set_port_feature(address, port,
                                      USB_HUB_FEATURE_PORT_POWER, 1)) {
            serial_write("EHCI: Failed to power hub port ");
            serial_write_dec(port);
            serial_write(".\n");
        }
    }

    ehci_delay_ms((uint16_t)(hub_descriptor[5] * 2 + 10));
    for (uint8_t port = 1; port <= ehci_hub_port_count; port++) {
        uint8_t status[4] = { 0, 0, 0, 0 };
        if (!ehci_hub_get_port_status(address, port, status)) {
            serial_write("EHCI: Failed to query hub port ");
            serial_write_dec(port);
            serial_write(".\n");
            continue;
        }

        uint16_t port_status = (uint16_t)status[0] | ((uint16_t)status[1] << 8);
        uint16_t port_change = (uint16_t)status[2] | ((uint16_t)status[3] << 8);
        serial_write("EHCI Hub port ");
        serial_write_dec(port);
        serial_write((port_status & 1) ? ": device connected\n" : ": empty\n");

        if ((port_change & (1 << 0))
            && !ehci_hub_set_port_feature(address, port, 16, 0)) {
            serial_write("EHCI: Failed to clear connection-change status.\n");
        }
        if (!(port_status & 1)) continue;

        if (!ehci_hub_set_port_feature(address, port,
                                      USB_HUB_FEATURE_PORT_RESET, 1)) {
            serial_write("EHCI: Failed to reset hub port ");
            serial_write_dec(port);
            serial_write(".\n");
            continue;
        }
        ehci_delay_ms(50);
        if (!ehci_hub_set_port_feature(address, port,
                                      USB_HUB_FEATURE_C_PORT_RESET, 0)) {
            serial_write("EHCI: Failed to clear hub reset-change status.\n");
        }
        if (ehci_hub_get_port_status(address, port, status)) {
            port_status = (uint16_t)status[0] | ((uint16_t)status[1] << 8);
            serial_write("EHCI Hub port ");
            serial_write_dec(port);
            serial_write((port_status & (1 << 1))
                ? ": downstream port enabled\n" : ": downstream port not enabled\n");
            if (port_status & (1 << 1)) ehci_enumerate_hub_mouse(port);
        }
    }
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

            ehci_log_controller_state("before-port-reset");
            ehci_log_portsc(i + 1, "before-reset", *portsc);
            // EHCI requires at least 50 ms of port reset before software clears PR.
            *portsc = EHCI_PORT_POWER | EHCI_PORT_RESET;
            ehci_log_portsc(i + 1, "reset-asserted", *portsc);
            if (!ehci_delay_ms(50)) {
                serial_write(", PIT timeout during port reset");
            }
            ehci_log_controller_state("port-reset-50ms");
            ehci_log_portsc(i + 1, "reset-50ms", *portsc);
            *portsc = EHCI_PORT_POWER;
            ehci_log_controller_state("after-reset-clear-write");
            ehci_log_portsc(i + 1, "reset-cleared", *portsc);
            if (!ehci_delay_ms(2)) {
                serial_write(", PIT timeout during port recovery");
            }
            ehci_log_portsc(i + 1, "recovery-2ms", *portsc);

            uint32_t timeout = 100000;
            while (!(*portsc & (EHCI_PORT_ENABLED | EHCI_PORT_OWNER)) && --timeout);
            val = *portsc;
            ehci_log_portsc(i + 1, "final", val);
            if (val & EHCI_PORT_OWNER) {
                serial_write(", handed to companion controller\n");
                continue;
            }

            if (timeout) {
                serial_write(", port enabled\n");
                if (!ehci_device_addressed) {
                    uint8_t port_speed = (uint8_t)((val >> 26) & 0x03);
                    serial_write("EHCI Port Speed detected: ");
                    serial_write_dec(port_speed);
                    serial_write("\n");
                    // Düşük/tam hızlı cihazlar için ilk kontrol paketi boyutu 8 bayt olmalıdır; yüksek hız için 64 bayt.
                    uint8_t initial_max_packet = (port_speed == 2) ? 64 : 8;

                    if (ehci_set_address(1, port_speed, initial_max_packet, 0, 0)) {
                        serial_write("EHCI: Device is ready for descriptor requests (Speed: ");
                        serial_write_dec(port_speed);
                        serial_write(").\n");
                        
                        if (ehci_get_device_descriptor(1, port_speed, initial_max_packet, 0, 0, 18)) {
                            if (device_descriptor[4] == USB_HUB_CLASS) {
                                ehci_enumerate_hub(1);
                            } else {
                                serial_write("EHCI: Connected device is not a hub.\n");
                            }
                        } else {
                            serial_write("EHCI: Failed to read device descriptor.\n");
                        }
                    } else {
                        serial_write("EHCI: SET_ADDRESS failed for connected device.\n");
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
    for (uint32_t i = 0; i < sizeof(async_head); i++) head_bytes[i] = 0;
    for (uint32_t i = 0; i < sizeof(control_qh); i++) qh_bytes[i] = 0;
    for (uint32_t i = 0; i < sizeof(control_setup_qtd); i++) {
        ((uint8_t*)&control_setup_qtd)[i] = 0;
        ((uint8_t*)&control_data_qtd)[i] = 0;
        ((uint8_t*)&control_status_qtd)[i] = 0;
    }

    uint32_t async_head_phys = (uint32_t)&async_head;
    uint32_t control_qh_phys = (uint32_t)&control_qh;
    async_head.horizontal_link = control_qh_phys | (1 << 1);
    async_head.endpoint_chars = 1 << 15;
    async_head.next_dtd = 1;
    async_head.alt_next_dtd = 1;
    control_qh.horizontal_link = async_head_phys | (1 << 1);
    control_qh.endpoint_chars = (2 << 12) | (64 << 16);
    control_qh.next_dtd = 1;
    control_qh.alt_next_dtd = 1;

    // ASYNCLISTADDR register'ına QH adresini yaz
    *((volatile uint32_t*)(ehci_op_base + EHCI_ASYNCLISTADDR)) = async_head_phys;

    // USBCMD içerisinden Asynchronous Schedule Enable (Bit 5) bitini set et
    uint32_t usbcmd = *((volatile uint32_t*)(ehci_op_base + EHCI_USBCMD));
    usbcmd |= (1 << 5); // Asynchronous Schedule Enable
    *((volatile uint32_t*)(ehci_op_base + EHCI_USBCMD)) = usbcmd;

    serial_write("EHCI: Asynchronous Schedule enabled successfully.\n");
}

static void ehci_probe_additional_controllers(pci_device_t* primary) {
    for (uint32_t index = 0; index < pci_get_device_count(); index++) {
        pci_device_t* dev = pci_get_device_by_index(index);
        if (!dev || dev == primary || dev->class_code != 0x0C
            || dev->subclass != 0x03 || dev->prog_if != 0x20) {
            continue;
        }

        uint32_t bar0 = dev->bar[0];
        if ((bar0 & 1) || (bar0 & ~0x0F) == 0) {
            serial_write("EHCI: Additional controller has invalid MMIO BAR0.\n");
            continue;
        }

        volatile uint8_t* mmio = (volatile uint8_t*)(bar0 & ~0x0F);
        uint8_t cap_len = *((volatile uint8_t*)(mmio + EHCI_CAPLENGTH));
        volatile uint8_t* op = mmio + cap_len;
        uint32_t hcsparams = *((volatile uint32_t*)(mmio + EHCI_HCSPARAMS));
        uint32_t hccparams = *((volatile uint32_t*)(mmio + EHCI_HCCPARAMS));
        uint8_t port_count = hcsparams & 0x0F;

        serial_write("EHCI: Probing additional controller PCI ");
        serial_write_dec(dev->bus);
        serial_write(":");
        serial_write_dec(dev->slot);
        serial_write(".");
        serial_write_dec(dev->func);
        serial_write("\n");

        if (!ehci_handoff_to_os(dev, hccparams)) {
            serial_write("EHCI: Could not take ownership of additional controller.\n");
            continue;
        }

        serial_write("EHCI: Additional controller root ports=");
        serial_write_dec(port_count);
        serial_write("\n");
        for (uint8_t port = 0; port < port_count; port++) {
            volatile uint32_t* portsc = (volatile uint32_t*)(op + EHCI_PORTSC + port * 4);
            uint32_t value = *portsc;
            uint8_t speed = (uint8_t)((value >> 26) & 0x03);

            serial_write("EHCI extra port ");
            serial_write_dec(port + 1);
            serial_write(" PORTSC=0x");
            serial_write_hex8(value >> 24);
            serial_write_hex8(value >> 16);
            serial_write_hex8(value >> 8);
            serial_write_hex8(value);
            serial_write(" connected=");
            serial_write((value & EHCI_PORT_CONNECT) ? "yes" : "no");
            serial_write(" speed=");
            if (!(value & EHCI_PORT_CONNECT)) {
                serial_write("n/a");
            } else if (speed == 0) {
                serial_write("full");
            } else if (speed == 1) {
                serial_write("low");
            } else if (speed == 2) {
                serial_write("high");
            } else {
                serial_write("reserved");
            }
            serial_write("\n");
        }
    }
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

    uint32_t hcsparams = *((volatile uint32_t*)(ehci_mmio_base + EHCI_HCSPARAMS));
    uint8_t companion_count = (uint8_t)((hcsparams >> 8) & 0x0F);
    uint8_t ports_per_companion = (uint8_t)((hcsparams >> 12) & 0x0F);
    serial_write("EHCI: HCSPARAMS=0x");
    serial_write_hex8(hcsparams >> 24);
    serial_write_hex8(hcsparams >> 16);
    serial_write_hex8(hcsparams >> 8);
    serial_write_hex8(hcsparams);
    serial_write(" companions=");
    serial_write_dec(companion_count);
    serial_write(" ports-per-companion=");
    serial_write_dec(ports_per_companion);
    serial_write("\n");

    uint32_t hccparams = *((volatile uint32_t*)(ehci_mmio_base + EHCI_HCCPARAMS));
    serial_write("EHCI: HCCPARAMS=0x");
    serial_write_hex8(hccparams >> 24);
    serial_write_hex8(hccparams >> 16);
    serial_write_hex8(hccparams >> 8);
    serial_write_hex8(hccparams);
    serial_write("\n");
    if (!ehci_handoff_to_os(dev, hccparams)) return false;

    // Stop the controller and disable both schedules before resetting it.
    uint32_t usbcmd = *((volatile uint32_t*)(ehci_op_base + EHCI_USBCMD));
    usbcmd &= ~((1 << 0) | (1 << 4) | (1 << 5));
    *((volatile uint32_t*)(ehci_op_base + EHCI_USBCMD)) = usbcmd;

    uint32_t timeout = 100000;
    while (!(*((volatile uint32_t*)(ehci_op_base + EHCI_USBSTS))
             & EHCI_USBSTS_HALTED) && --timeout);
    if (!timeout) {
        serial_write("EHCI Error: Controller did not halt before reset.\n");
        return false;
    }

    serial_write("EHCI: Resetting host controller...\n");
    *((volatile uint32_t*)(ehci_op_base + EHCI_USBCMD)) = EHCI_USBCMD_RESET;
    timeout = 100000;
        while ((*((volatile uint32_t*)(ehci_op_base + EHCI_USBCMD))
            & EHCI_USBCMD_RESET) && --timeout);
    if (!timeout) {
        serial_write("EHCI Error: Host controller reset timed out.\n");
        return false;
    }

    uint32_t usbsts = *((volatile uint32_t*)(ehci_op_base + EHCI_USBSTS));
    *((volatile uint32_t*)(ehci_op_base + EHCI_USBSTS)) = usbsts & EHCI_USBSTS_W1C_MASK;

    // Route connected ports to the EHCI controller.
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

    // Start the controller and require HCHalted to clear before using ports.
    usbcmd = *((volatile uint32_t*)(ehci_op_base + EHCI_USBCMD));
    usbcmd |= EHCI_USBCMD_RUN;
    *((volatile uint32_t*)(ehci_op_base + EHCI_USBCMD)) = usbcmd;

    timeout = 1000000;
        while ((*((volatile uint32_t*)(ehci_op_base + EHCI_USBSTS))
            & EHCI_USBSTS_HALTED) && --timeout);

    usbcmd = *((volatile uint32_t*)(ehci_op_base + EHCI_USBCMD));
    usbsts = *((volatile uint32_t*)(ehci_op_base + EHCI_USBSTS));
    if (!timeout || (usbsts & EHCI_USBSTS_HOST_ERROR)) {
        serial_write("EHCI Error: Controller failed to run. USBCMD=0x");
        serial_write_hex8(usbcmd >> 24);
        serial_write_hex8(usbcmd >> 16);
        serial_write_hex8(usbcmd >> 8);
        serial_write_hex8(usbcmd);
        serial_write(" USBSTS=0x");
        serial_write_hex8(usbsts >> 24);
        serial_write_hex8(usbsts >> 16);
        serial_write_hex8(usbsts >> 8);
        serial_write_hex8(usbsts);
        serial_write("\n");
        return false;
    }

    serial_write("EHCI: Controller started and running.\n");

    // 4. Kontrolcü çalıştıktan sonra portlara güç ver ve durumları tara
    ehci_check_ports();
    ehci_probe_additional_controllers(dev);

    return true;
}

uint8_t ehci_mouse_active(void) {
    return ehci_mouse_ready;
}

uint8_t ehci_poll(void) {
    if (!ehci_mouse_ready) return 0;

    uint32_t token = mouse_interrupt_qtd.token;
    if (token & EHCI_QTD_STATUS_ACTIVE) return 0;

    uint16_t remaining = (uint16_t)((token >> 16) & 0x7FFF);
    uint8_t report_length = remaining <= ehci_mouse_max_packet
        ? (uint8_t)(ehci_mouse_max_packet - remaining) : 0;
    uint8_t transfer_ok = (token & 0x7F) == 0;
    uint8_t report_valid = transfer_ok && report_length >= 3;

    if (transfer_ok) {
        ehci_mouse_data_toggle ^= 1;
    } else if (!ehci_mouse_error_logged) {
        serial_write("EHCI: Mouse interrupt qTD reported an error.\n");
        ehci_mouse_error_logged = 1;
    }

    if (report_valid) {
        usb_mouse_process_report(mouse_interrupt_buffer, report_length);
    }

    mouse_interrupt_qtd.next_dtd = 1;
    mouse_interrupt_qtd.alt_next_dtd = 1;
    mouse_interrupt_qtd.token = EHCI_QTD_STATUS_ACTIVE | EHCI_QTD_IOC
        | EHCI_QTD_LENGTH(ehci_mouse_max_packet) | EHCI_QTD_PID_IN | (3 << 10)
        | (ehci_mouse_data_toggle ? EHCI_QTD_TOGGLE_1 : EHCI_QTD_TOGGLE_0);
    ehci_set_qtd_buffer(&mouse_interrupt_qtd, mouse_interrupt_buffer);
    mouse_interrupt_qh.current_dtd = 0;
    mouse_interrupt_qh.next_dtd = (uint32_t)&mouse_interrupt_qtd;
    mouse_interrupt_qh.alt_next_dtd = 1;
    mouse_interrupt_qh.token = 0;
    __asm__ volatile("" ::: "memory");
    return report_valid;
}