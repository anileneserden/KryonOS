#include <kernel/drivers/usb/ehci.h>
#include <kernel/drivers/pci.h>
#include <kernel/serial.h>
#include <kernel/mem/heap.h>
#include <kernel/drivers/input/mouse_usb.h>
#include <arch/x86/io.h>
#include <stdint.h>
#include <stddef.h>

#ifndef EHCI_LOG_ENABLED
#define EHCI_LOG_ENABLED 1
#endif

#define EHCI_LOG(...) do { if (EHCI_LOG_ENABLED) { __VA_ARGS__ } } while (0)

#define CMD_RS      (1U << 0)
#define CMD_HCRESET (1U << 1)
#define CMD_PSE     (1U << 4)
#define CMD_ASE     (1U << 5)
#define CMD_IAAD    (1U << 6)
#define STS_IAA     (1U << 5)
#define STS_HALTED  (1U << 12)

// CSC, PEC and OCC are write-1-to-clear and must be masked on every PORTSC write.
#define PORT_CCS    (1U << 0)
#define PORT_PE     (1U << 2)
#define PORT_RESET  (1U << 8)
#define PORT_POWER  (1U << 12)
#define PORT_OWNER  (1U << 13)
#define PORT_W1C    ((1U << 1) | (1U << 3) | (1U << 5))

#define QTD_ACTIVE  (1U << 7)
#define QTD_HALTED  (1U << 6)
#define QTD_ERR     (0x78U)

#define PID_OUT   0
#define PID_IN    1
#define PID_SETUP 2

#define SPEED_FULL 0
#define SPEED_LOW  1
#define SPEED_HIGH 2

typedef struct {
    volatile uint8_t  caplength;
    volatile uint8_t  reserved;
    volatile uint16_t hciversion;
    volatile uint32_t hcsparams;
    volatile uint32_t hccparams;
} __attribute__((packed)) ehci_caps_t;

typedef struct {
    volatile uint32_t usbcmd;
    volatile uint32_t usbsts;
    volatile uint32_t usbintr;
    volatile uint32_t frindex;
    volatile uint32_t ctrldssegment;
    volatile uint32_t periodiclistbase;
    volatile uint32_t asynclistaddr;
    volatile uint8_t  reserved[0x24];
    volatile uint32_t configflag;
    volatile uint32_t portsc[16];
} __attribute__((packed)) ehci_op_regs_t;

// The extended buffer pointers keep the layout valid for 64-bit capable controllers.
typedef struct {
    volatile uint32_t next;
    volatile uint32_t alt_next;
    volatile uint32_t token;
    volatile uint32_t buffer[5];
    volatile uint32_t buffer_hi[5];
} __attribute__((packed, aligned(32))) ehci_qtd_t;

typedef struct {
    volatile uint32_t hlink;
    volatile uint32_t ep_chars;
    volatile uint32_t ep_caps;
    volatile uint32_t current_qtd;
    volatile uint32_t next_qtd;
    volatile uint32_t alt_next_qtd;
    volatile uint32_t token;
    volatile uint32_t buffer[5];
    volatile uint32_t buffer_hi[5];
} __attribute__((packed, aligned(32))) ehci_qh_t;

typedef struct {
    uint8_t addr;
    uint8_t speed;
    uint8_t mps0;
    uint8_t hub_addr; // transaction translator hub, only for non-high-speed devices
    uint8_t hub_port;
} ehci_dev_t;

static volatile ehci_caps_t *ehci_caps;
static volatile ehci_op_regs_t *ehci_op;
static uint8_t ehci_active = 0;
static uint8_t next_addr = 1;

static uint32_t *periodic_list;
static ehci_qh_t *async_head;
static ehci_qh_t *ctrl_qh;
static ehci_qtd_t *ctrl_qtd; // [0]=setup, [1]=data, [2]=status
static uint8_t *ctrl_setup;
static uint8_t *ctrl_buf;

static ehci_qh_t *int_qh;
static ehci_qtd_t *int_qtd;
static uint8_t *int_buf;
static uint16_t int_len;
static uint8_t int_toggle;
static uint8_t int_ready;

static uint8_t cfg_buf[256];

static inline uint32_t virt_to_phys(volatile void *virt_addr) {
    return (uint32_t)(uintptr_t)virt_addr;
}

static void *ehci_alloc_aligned(size_t size, uintptr_t alignment) {
    void *allocation = kzalloc(size + alignment - 1);
    if (!allocation) return NULL;
    return (void *)(((uintptr_t)allocation + alignment - 1) & ~(alignment - 1));
}

// A write to port 0x80 takes about 1us on PC hardware.
static void ehci_delay_ms(uint32_t ms) {
    for (uint32_t i = 0; i < ms * 1000; i++) outb(0x80, 0);
}

int ehci_detect_controller(void) {
    return pci_get_device_by_class(EHCI_CLASS_CODE, EHCI_SUBCLASS, EHCI_PROG_IF) != NULL;
}

uint8_t ehci_is_active(void) {
    return ehci_active;
}

static void ehci_fill_qtd(ehci_qtd_t *q, uint32_t next, uint32_t alt, uint8_t pid,
                          uint8_t toggle, uint16_t len, void *buf, uint8_t ioc) {
    uint32_t addr = virt_to_phys(buf);
    q->next = next;
    q->alt_next = alt;
    q->buffer[0] = addr;
    for (int i = 1; i < 5; i++) q->buffer[i] = (addr + 0x1000U * i) & ~0xFFFU;
    for (int i = 0; i < 5; i++) q->buffer_hi[i] = 0;
    q->token = ((uint32_t)toggle << 31) | ((uint32_t)len << 16) | ((uint32_t)ioc << 15) |
               (3U << 10) | ((uint32_t)pid << 8) | QTD_ACTIVE;
}

static void ehci_async_doorbell(void) {
    ehci_op->usbsts = STS_IAA;
    ehci_op->usbcmd |= CMD_IAAD;
    for (int i = 0; i < 100 && !(ehci_op->usbsts & STS_IAA); i++) ehci_delay_ms(1);
    ehci_op->usbsts = STS_IAA;
}

// Returns the number of bytes received in the data stage (0 without one), or -1 on failure.
static int ehci_control(const ehci_dev_t *dev, uint8_t req_type, uint8_t req,
                        uint16_t value, uint16_t index, uint16_t len, void *out) {
    if (len > 256) return -1;

    ctrl_setup[0] = req_type;
    ctrl_setup[1] = req;
    ctrl_setup[2] = value & 0xFF;
    ctrl_setup[3] = value >> 8;
    ctrl_setup[4] = index & 0xFF;
    ctrl_setup[5] = index >> 8;
    ctrl_setup[6] = len & 0xFF;
    ctrl_setup[7] = len >> 8;

    ehci_qtd_t *setup = &ctrl_qtd[0];
    ehci_qtd_t *data = &ctrl_qtd[1];
    ehci_qtd_t *status = &ctrl_qtd[2];

    ehci_fill_qtd(status, 1, 1, len ? PID_OUT : PID_IN, 1, 0, ctrl_buf, 0);
    uint32_t setup_next = virt_to_phys(status);
    if (len) {
        // A short data packet must fall through to the status stage.
        ehci_fill_qtd(data, virt_to_phys(status), virt_to_phys(status), PID_IN, 1, len, ctrl_buf, 0);
        setup_next = virt_to_phys(data);
    }
    ehci_fill_qtd(setup, setup_next, 1, PID_SETUP, 0, 8, ctrl_setup, 0);

    ctrl_qh->ep_chars = dev->addr | ((uint32_t)dev->speed << 12) | (1U << 14) |
                        ((uint32_t)dev->mps0 << 16) | (dev->speed != SPEED_HIGH ? (1U << 27) : 0);
    ctrl_qh->ep_caps = (1U << 30) | ((uint32_t)dev->hub_port << 23) | ((uint32_t)dev->hub_addr << 16);
    ctrl_qh->current_qtd = 0;
    ctrl_qh->alt_next_qtd = 1;
    ctrl_qh->token = 0;
    ctrl_qh->next_qtd = virt_to_phys(setup);

    ctrl_qh->hlink = async_head->hlink;
    async_head->hlink = virt_to_phys(ctrl_qh) | 2;

    int ok = 0;
    for (int i = 0; i < 500; i++) {
        if (!(status->token & QTD_ACTIVE)) { ok = !(status->token & QTD_ERR); break; }
        if ((setup->token | (len ? data->token : 0)) & QTD_ERR) break;
        if (ctrl_qh->token & QTD_HALTED) break;
        ehci_delay_ms(1);
    }

    async_head->hlink = virt_to_phys(async_head) | 2;
    ehci_async_doorbell();

    if (!ok) return -1;
    if (!len) return 0;

    int got = len - ((data->token >> 16) & 0x7FFF);
    if (out) for (int i = 0; i < got; i++) ((uint8_t *)out)[i] = ctrl_buf[i];
    return got;
}

static void ehci_arm_interrupt(void) {
    ehci_fill_qtd(int_qtd, 1, 1, PID_IN, int_toggle, int_len, int_buf, 1);
    int_qh->current_qtd = 0;
    int_qh->alt_next_qtd = 1;
    int_qh->token = 0;
    int_qh->next_qtd = virt_to_phys(int_qtd);
}

static void ehci_setup_interrupt(const ehci_dev_t *dev, uint8_t ep, uint16_t mps) {
    int_len = mps > 64 ? 64 : mps;
    int_toggle = 0;

    int_qh->hlink = 1;
    int_qh->ep_chars = dev->addr | ((uint32_t)ep << 8) | ((uint32_t)dev->speed << 12) |
                       (1U << 14) | ((uint32_t)mps << 16);
    uint32_t caps = (1U << 30) | 0x01; // Mult=1, start-split in microframe 0
    if (dev->speed != SPEED_HIGH) {
        caps |= (0x1CU << 8) | ((uint32_t)dev->hub_port << 23) | ((uint32_t)dev->hub_addr << 16);
    }
    int_qh->ep_caps = caps;
    ehci_arm_interrupt();

    uint32_t qh_ptr = virt_to_phys(int_qh) | 2;
    for (int i = 0; i < 1024; i++) periodic_list[i] = qh_ptr;
    int_ready = 1;
}

static void ehci_enum_hub(const ehci_dev_t *hub, int depth);

static void ehci_bind_hid(const ehci_dev_t *dev, uint16_t total) {
    uint8_t in_hid = 0, iface = 0, subclass = 0, proto = 0;

    for (uint16_t i = 0; i + 2 <= total && cfg_buf[i] >= 2; i += cfg_buf[i]) {
        uint8_t type = cfg_buf[i + 1];
        if (type == 4 && cfg_buf[i] >= 9) {
            iface = cfg_buf[i + 2];
            in_hid = (cfg_buf[i + 5] == 3);
            subclass = cfg_buf[i + 6];
            proto = cfg_buf[i + 7];
        } else if (type == 5 && cfg_buf[i] >= 7 && in_hid && proto != 1 &&
                   (cfg_buf[i + 3] & 3) == 3 && (cfg_buf[i + 2] & 0x80)) {
            uint8_t ep = cfg_buf[i + 2] & 0x0F;
            uint16_t mps = (cfg_buf[i + 4] | (cfg_buf[i + 5] << 8)) & 0x7FF;

            if (subclass == 1) ehci_control(dev, 0x21, 0x0B, 0, iface, 0, NULL); // boot protocol
            ehci_control(dev, 0x21, 0x0A, 0, iface, 0, NULL); // SET_IDLE

            // A boot mouse reports relative motion; other pointers (e.g. tablets) are absolute.
            usb_mouse_set_absolute_mode(!(subclass == 1 && proto == 2));
            ehci_setup_interrupt(dev, ep, mps);
            EHCI_LOG(
                serial_write("[EHCI] HID pointer bound: addr ");
                serial_write_dec(dev->addr);
                serial_write(" ep ");
                serial_write_dec(ep);
                serial_write(subclass == 1 && proto == 2 ? " (relative mouse)\n" : " (absolute pointer)\n");
            );
            return;
        }
    }
}

static int ehci_enumerate(uint8_t speed, uint8_t hub_addr, uint8_t hub_port, int depth) {
    ehci_dev_t dev = { 0, speed, speed == SPEED_HIGH ? 64 : 8, hub_addr, hub_port };
    uint8_t desc[18];

    if (next_addr > 127) return 0;

    int got = -1;
    for (int attempt = 0; attempt < 3 && got < 8; attempt++) {
        got = ehci_control(&dev, 0x80, 6, 0x0100, 0, 8, desc);
        if (got < 8) ehci_delay_ms(10);
    }
    if (got < 8 || desc[7] == 0) {
        serial_write("[EHCI] ERROR: device did not answer at address 0.\n");
        return 0;
    }
    dev.mps0 = desc[7];

    uint8_t addr = next_addr++;
    if (ehci_control(&dev, 0x00, 5, addr, 0, 0, NULL) < 0) {
        serial_write("[EHCI] ERROR: SET_ADDRESS failed.\n");
        return 0;
    }
    ehci_delay_ms(10);
    dev.addr = addr;

    if (ehci_control(&dev, 0x80, 6, 0x0100, 0, 18, desc) < 18) {
        serial_write("[EHCI] ERROR: device descriptor read failed.\n");
        return 0;
    }

    EHCI_LOG(
        serial_write("[EHCI] Device addr ");
        serial_write_dec(addr);
        serial_write(" VID:PID ");
        serial_write_hex16(desc[8] | (desc[9] << 8));
        serial_write(":");
        serial_write_hex16(desc[10] | (desc[11] << 8));
        serial_write(" class ");
        serial_write_dec(desc[4]);
        serial_write("\n");
    );

    if (ehci_control(&dev, 0x80, 6, 0x0200, 0, 9, cfg_buf) < 9) {
        serial_write("[EHCI] ERROR: config descriptor read failed.\n");
        return 0;
    }
    uint16_t total = cfg_buf[2] | (cfg_buf[3] << 8);
    if (total > sizeof(cfg_buf)) total = sizeof(cfg_buf);
    if (ehci_control(&dev, 0x80, 6, 0x0200, 0, total, cfg_buf) < 9) {
        serial_write("[EHCI] ERROR: full config descriptor read failed.\n");
        return 0;
    }

    if (ehci_control(&dev, 0x00, 9, cfg_buf[5], 0, 0, NULL) < 0) {
        serial_write("[EHCI] ERROR: SET_CONFIGURATION failed.\n");
        return 0;
    }

    if (desc[4] == 9) {
        ehci_enum_hub(&dev, depth);
    } else if (!int_ready) {
        ehci_bind_hid(&dev, total);
    }
    return 1;
}

static void ehci_enum_hub(const ehci_dev_t *hub, int depth) {
    uint8_t d[8];
    uint8_t st[4];

    if (depth >= 1) return;
    if (ehci_control(hub, 0xA0, 6, 0x2900, 0, 8, d) < 8) {
        serial_write("[EHCI] ERROR: hub descriptor read failed.\n");
        return;
    }

    uint8_t nports = d[2] > 15 ? 15 : d[2];
    uint32_t power_ms = d[5] * 2U;
    if (power_ms < 100) power_ms = 100;

    EHCI_LOG(
        serial_write("[EHCI] Hub with ");
        serial_write_dec(nports);
        serial_write(" ports\n");
    );

    for (uint8_t p = 1; p <= nports; p++) ehci_control(hub, 0x23, 3, 8, p, 0, NULL); // PORT_POWER
    ehci_delay_ms(power_ms);

    for (uint8_t p = 1; p <= nports; p++) {
        if (ehci_control(hub, 0xA3, 0, 0, p, 4, st) < 4 || !(st[0] & 1)) continue;

        ehci_control(hub, 0x23, 3, 4, p, 0, NULL); // PORT_RESET
        ehci_delay_ms(50);
        for (int t = 0; t < 20; t++) {
            if (ehci_control(hub, 0xA3, 0, 0, p, 4, st) >= 4 && !(st[0] & 0x10)) break;
            ehci_delay_ms(10);
        }
        ehci_control(hub, 0x23, 1, 20, p, 0, NULL); // C_PORT_RESET
        ehci_control(hub, 0x23, 1, 16, p, 0, NULL); // C_PORT_CONNECTION
        ehci_delay_ms(10);

        if (ehci_control(hub, 0xA3, 0, 0, p, 4, st) < 4 || !(st[0] & 2)) continue;

        uint8_t speed = (st[1] & 0x04) ? SPEED_HIGH : (st[1] & 0x02) ? SPEED_LOW : SPEED_FULL;
        EHCI_LOG(
            serial_write("[EHCI] Hub port ");
            serial_write_dec(p);
            serial_write(" device speed ");
            serial_write_dec(speed);
            serial_write("\n");
        );

        if (speed == SPEED_HIGH) ehci_enumerate(speed, 0, 0, depth + 1);
        else ehci_enumerate(speed, hub->addr, p, depth + 1);
    }
}

static int ehci_bios_handoff(pci_device_t *pci_dev) {
    uint8_t eecp = (ehci_caps->hccparams >> 8) & 0xFF;
    if (eecp < 0x40) return 1;

    uint32_t legsup = pci_read_config32(pci_dev->bus, pci_dev->slot, pci_dev->func, eecp);
    if ((legsup & 0xFF) != 1) return 1;

    pci_write_config32(pci_dev->bus, pci_dev->slot, pci_dev->func, eecp, legsup | (1U << 24));
    for (int i = 0; i < 1000; i++) {
        legsup = pci_read_config32(pci_dev->bus, pci_dev->slot, pci_dev->func, eecp);
        if (!(legsup & (1U << 16))) return 1;
        ehci_delay_ms(1);
    }
    return 0;
}

void ehci_init(void) {
    EHCI_LOG(serial_write("[EHCI] Initializing EHCI driver...\n"););

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

    uintptr_t mmio_base = bar0 & 0xFFFFFFF0;
    ehci_caps = (volatile ehci_caps_t *)mmio_base;
    if (ehci_caps->caplength < 0x10) {
        serial_write("[EHCI] ERROR: Invalid capability register length.\n");
        return;
    }
    ehci_op = (volatile ehci_op_regs_t *)(mmio_base + ehci_caps->caplength);

    if (!ehci_bios_handoff(pci_dev)) {
        serial_write("[EHCI] WARNING: BIOS did not release the controller.\n");
    }

    ehci_op->usbintr = 0;
    ehci_op->usbcmd &= ~CMD_RS;
    int ok = 0;
    for (int i = 0; i < 100 && !ok; i++) {
        ok = (ehci_op->usbsts & STS_HALTED) != 0;
        if (!ok) ehci_delay_ms(1);
    }
    if (!ok) {
        serial_write("[EHCI] ERROR: Controller did not halt.\n");
        return;
    }

    ehci_op->usbcmd |= CMD_HCRESET;
    ok = 0;
    for (int i = 0; i < 250 && !ok; i++) {
        ok = !(ehci_op->usbcmd & CMD_HCRESET);
        if (!ok) ehci_delay_ms(1);
    }
    if (!ok) {
        serial_write("[EHCI] ERROR: Controller reset timed out.\n");
        return;
    }

    periodic_list = ehci_alloc_aligned(1024 * sizeof(uint32_t), 4096);
    async_head = ehci_alloc_aligned(sizeof(ehci_qh_t), 32);
    ctrl_qh = ehci_alloc_aligned(sizeof(ehci_qh_t), 32);
    ctrl_qtd = ehci_alloc_aligned(sizeof(ehci_qtd_t) * 3, 32);
    ctrl_setup = ehci_alloc_aligned(8, 32);
    ctrl_buf = ehci_alloc_aligned(256, 256);
    int_qh = ehci_alloc_aligned(sizeof(ehci_qh_t), 32);
    int_qtd = ehci_alloc_aligned(sizeof(ehci_qtd_t), 32);
    int_buf = ehci_alloc_aligned(64, 64);

    if (!periodic_list || !async_head || !ctrl_qh || !ctrl_qtd || !ctrl_setup ||
        !ctrl_buf || !int_qh || !int_qtd || !int_buf) {
        serial_write("[EHCI] ERROR: Memory allocation failed.\n");
        return;
    }

    for (int i = 0; i < 1024; i++) periodic_list[i] = 1;

    // The async list head has the H bit set and stays halted so the controller never executes it.
    async_head->hlink = virt_to_phys(async_head) | 2;
    async_head->ep_chars = (1U << 15);
    async_head->current_qtd = 1;
    async_head->next_qtd = 1;
    async_head->alt_next_qtd = 1;
    async_head->token = QTD_HALTED;

    ehci_op->ctrldssegment = 0;
    ehci_op->periodiclistbase = virt_to_phys(periodic_list);
    ehci_op->asynclistaddr = virt_to_phys(async_head);

    ehci_op->usbcmd = CMD_RS | CMD_PSE | CMD_ASE | (8U << 16);
    for (int i = 0; i < 100 && (ehci_op->usbsts & STS_HALTED); i++) ehci_delay_ms(1);
    ehci_op->configflag = 1;
    ehci_delay_ms(5);
    ehci_active = 1;

    uint8_t n_ports = ehci_caps->hcsparams & 0x0F;
    EHCI_LOG(
        serial_write("[EHCI] Root ports: ");
        serial_write_dec(n_ports);
        serial_write("\n");
    );

    for (uint8_t i = 0; i < n_ports; i++) {
        ehci_op->portsc[i] = (ehci_op->portsc[i] & ~PORT_W1C) | PORT_POWER;
    }
    ehci_delay_ms(100);

    for (uint8_t i = 0; i < n_ports; i++) {
        uint32_t v = ehci_op->portsc[i];
        if (!(v & PORT_CCS)) continue;

        // K-state line status means a low-speed device, which only the companion controller can drive.
        if (((v >> 10) & 3) == 1) {
            ehci_op->portsc[i] = (v & ~PORT_W1C) | PORT_OWNER;
            continue;
        }

        ehci_op->portsc[i] = (v & ~(PORT_W1C | PORT_PE)) | PORT_RESET;
        ehci_delay_ms(50);
        ehci_op->portsc[i] = ehci_op->portsc[i] & ~(PORT_W1C | PORT_RESET);
        for (int t = 0; t < 20 && (ehci_op->portsc[i] & PORT_RESET); t++) ehci_delay_ms(1);
        ehci_delay_ms(10);

        v = ehci_op->portsc[i];
        if (!(v & PORT_PE)) {
            // Not high-speed: EHCI cannot see the device, so the companion controller takes the port.
            EHCI_LOG(
                serial_write("[EHCI] Root port ");
                serial_write_dec(i);
                serial_write(": full-speed device, releasing to companion controller\n");
            );
            ehci_op->portsc[i] = (v & ~PORT_W1C) | PORT_OWNER;
            continue;
        }

        EHCI_LOG(
            serial_write("[EHCI] Root port ");
            serial_write_dec(i);
            serial_write(": high-speed device\n");
        );
        ehci_enumerate(SPEED_HIGH, 0, 0, 0);
    }

    if (!int_ready) {
        serial_write("[EHCI] WARNING: no HID pointer device found.\n");
    }
    EHCI_LOG(serial_write("[EHCI] Controller running.\n"););
}

uint8_t ehci_poll(void) {
    if (!ehci_active || !int_ready) return 0;

    uint32_t token = int_qtd->token;
    if (token & QTD_ACTIVE) return 0;

    uint8_t updated = 0;
    if (!(token & QTD_ERR)) {
        uint16_t got = int_len - ((token >> 16) & 0x7FFF);
        int_toggle ^= 1;
        if (got >= 3) {
            usb_mouse_process_report(int_buf, (uint8_t)got);
            updated = 1;
        }
    } else {
        int_toggle = 0;
    }

    ehci_arm_interrupt();
    return updated;
}
