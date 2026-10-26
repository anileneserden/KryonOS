#include <kernel/power.h>
#include <kernel/multiboot.h>
#include <arch/x86/io.h>
#include <kernel/serial.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct __attribute__((packed)) {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} rsdp_v1_t;

typedef struct __attribute__((packed)) {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
} acpi_table_header_t;

#define ACPI_HEADER_SIZE 36

static uint32_t read_u32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint64_t read_u64(const uint8_t *data) {
    return (uint64_t)read_u32(data) | ((uint64_t)read_u32(data + 4) << 32);
}

static bool checksum_valid(const uint8_t *data, uint32_t length) {
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; i++) sum = (uint8_t)(sum + data[i]);
    return sum == 0;
}

static rsdp_v1_t *find_rsdp_in_range(uint32_t start, uint32_t end) {
    for (uint32_t address = start; address + sizeof(rsdp_v1_t) <= end; address += 16) {
        rsdp_v1_t *rsdp = (rsdp_v1_t *)address;
        if (rsdp->signature[0] != 'R' || rsdp->signature[1] != 'S' ||
            rsdp->signature[2] != 'D' || rsdp->signature[3] != ' ' ||
            rsdp->signature[4] != 'P' || rsdp->signature[5] != 'T' ||
            rsdp->signature[6] != 'R' || rsdp->signature[7] != ' ')
            continue;
        if (checksum_valid((const uint8_t *)rsdp, sizeof(rsdp_v1_t))) return rsdp;
    }
    return 0;
}

static uint32_t saved_mmap_addr;
static uint32_t saved_mmap_length;

void power_init(const multiboot_info_t *mboot) {
    if (mboot && (mboot->flags & (1 << 6))) {
        saved_mmap_addr = mboot->mmap_addr;
        saved_mmap_length = mboot->mmap_length;
    }
}

static rsdp_v1_t *find_rsdp(void) {
    uint16_t ebda_segment = *(volatile uint16_t *)0x40E;
    uint32_t ebda = (uint32_t)ebda_segment << 4;
    rsdp_v1_t *rsdp = 0;

    if (ebda >= 0x400 && ebda < 0xA0000)
        rsdp = find_rsdp_in_range(ebda, ebda + 1024);
    if (!rsdp) rsdp = find_rsdp_in_range(0xE0000, 0x100000);
    // Some firmware places the RSDP outside the standard areas.
    if (!rsdp) rsdp = find_rsdp_in_range(0x1000, 0xA0000);

    // Fall back to E820 ACPI reclaim (3) / NVS (4) regions below 4 GB.
    uint32_t offset = 0;
    while (!rsdp && offset + sizeof(multiboot_memory_map_t) <= saved_mmap_length) {
        multiboot_memory_map_t *entry = (multiboot_memory_map_t *)(saved_mmap_addr + offset);
        if ((entry->type == 3 || entry->type == 4) && entry->addr_high == 0 && entry->len_high == 0) {
            uint32_t start = entry->addr_low;
            uint32_t end = start + entry->len_low;
            if (end < start) end = 0xFFFFFFF0;
            serial_write("ACPI shutdown: scanning E820 ACPI region.\n");
            rsdp = find_rsdp_in_range(start, end);
        }
        offset += entry->size + 4;
    }
    return rsdp;
}

static acpi_table_header_t *find_acpi_table(uint32_t root_address, bool xsdt, const char *signature) {
    acpi_table_header_t *root = (acpi_table_header_t *)root_address;
    const char *root_sig = xsdt ? "XSDT" : "RSDT";
    if (!root_address || root->signature[0] != root_sig[0] || root->signature[1] != root_sig[1] ||
        root->signature[2] != root_sig[2] || root->signature[3] != root_sig[3] ||
        root->length < ACPI_HEADER_SIZE || root->length > 1024 * 1024) {
        serial_write(xsdt ? "ACPI shutdown: XSDT invalid.\n" : "ACPI shutdown: RSDT invalid.\n");
        return 0;
    }
    if (!checksum_valid((const uint8_t *)root, root->length))
        serial_write("ACPI shutdown: root table checksum bad, continuing.\n");

    uint32_t entry_size = xsdt ? 8 : 4;
    uint32_t count = (root->length - ACPI_HEADER_SIZE) / entry_size;
    const uint8_t *entries = (const uint8_t *)root + ACPI_HEADER_SIZE;
    serial_write(xsdt ? "ACPI: XSDT at " : "ACPI: RSDT at ");
    serial_write_hex(root_address);
    serial_write(" entries=");
    serial_write_dec(count);
    serial_write("\n");
    for (uint32_t i = 0; i < count; i++) {
        uint64_t entry = xsdt ? read_u64(entries + i * 8) : read_u32(entries + i * 4);
        serial_write("ACPI:  entry ");
        serial_write_hex((uint32_t)entry);
        if (entry && entry <= 0xFFFFFFF0u) {
            const char *s = (const char *)(uint32_t)entry;
            serial_write(" ");
            for (int k = 0; k < 4; k++) serial_write_char(s[k]);
        }
        serial_write("\n");
    }
    for (uint32_t i = 0; i < count; i++) {
        uint64_t entry = xsdt ? read_u64(entries + i * 8) : read_u32(entries + i * 4);
        if (!entry || entry > 0xFFFFFFF0u) continue;
        acpi_table_header_t *table = (acpi_table_header_t *)(uint32_t)entry;
        if (table->signature[0] == signature[0] &&
            table->signature[1] == signature[1] && table->signature[2] == signature[2] &&
            table->signature[3] == signature[3] && table->length >= sizeof(*table) &&
            table->length <= 1024 * 1024)
            return table;
    }
    return 0;
}

static bool aml_integer(const uint8_t **cursor, const uint8_t *end, uint8_t *value) {
    if (*cursor >= end) return false;
    uint8_t opcode = *(*cursor)++;
    if (opcode == 0x00 || opcode == 0x01) {
        *value = opcode;
        return true;
    }
    if (opcode == 0x0A && *cursor < end) {
        *value = *(*cursor)++;
        return true;
    }
    if (opcode == 0x0B && end - *cursor >= 2) {
        *value = (uint8_t)(*cursor)[0];
        *cursor += 2;
        return true;
    }
    if (opcode == 0x0C && end - *cursor >= 4) {
        *value = (uint8_t)(*cursor)[0];
        *cursor += 4;
        return true;
    }
    return false;
}

static bool find_s5_types(const uint8_t *dsdt, uint8_t *type_a, uint8_t *type_b) {
    if (dsdt[0] != 'D' || dsdt[1] != 'S' || dsdt[2] != 'D' || dsdt[3] != 'T') return false;
    uint32_t length = read_u32(dsdt + 4);
    if (length < ACPI_HEADER_SIZE || length > 1024 * 1024 ||
        !checksum_valid(dsdt, length)) return false;

    const uint8_t *end = dsdt + length;
    for (const uint8_t *p = dsdt + ACPI_HEADER_SIZE; end - p >= 6; p++) {
        if (p[0] != '_' || p[1] != 'S' || p[2] != '5' || p[3] != '_') continue;

        const uint8_t *package = p + 4;
        if (*package++ != 0x12 || package >= end) continue;
        uint8_t lead = *package++;
        uint32_t follow_bytes = lead >> 6;
        uint32_t package_length = lead & (follow_bytes ? 0x0F : 0x3F);
        if ((uint32_t)(end - package) < follow_bytes) continue;
        for (uint32_t i = 0; i < follow_bytes; i++)
            package_length |= (uint32_t)package[i] << (4 + 8 * i);
        package += follow_bytes;
        if (package >= end || package_length < 3 || *package < 2) continue;
        package++;
        if (!aml_integer(&package, end, type_a) || !aml_integer(&package, end, type_b)) continue;
        return true;
    }
    return false;
}

static bool acpi_power_off(void) {
    rsdp_v1_t *rsdp = find_rsdp();
    if (!rsdp) {
        serial_write("ACPI shutdown: RSDP not found in BIOS memory.\n");
        return false;
    }

    acpi_table_header_t *fadt_header = 0;
    serial_write("ACPI: RSDP at ");
    serial_write_hex((uint32_t)rsdp);
    serial_write(" rev=");
    serial_write_dec(rsdp->revision);
    serial_write(" rsdt=");
    serial_write_hex(rsdp->rsdt_address);
    serial_write("\n");
    const uint8_t *rsdp_bytes = (const uint8_t *)rsdp;
    if (rsdp->revision >= 2) {
        uint64_t xsdt_address = read_u64(rsdp_bytes + 24);
        if (xsdt_address && xsdt_address <= 0xFFFFFFF0u)
            fadt_header = find_acpi_table((uint32_t)xsdt_address, true, "FACP");
    }
    if (!fadt_header)
        fadt_header = find_acpi_table(rsdp->rsdt_address, false, "FACP");
    if (!fadt_header || fadt_header->length < 90) {
        serial_write("ACPI shutdown: valid FADT not found.\n");
        return false;
    }
    const uint8_t *fadt = (const uint8_t *)fadt_header;

    uint32_t dsdt_address = read_u32(fadt + 40);
    uint32_t pm1a_control = read_u32(fadt + 64);
    uint32_t pm1b_control = read_u32(fadt + 68);
    uint8_t pm1_control_length = fadt[89];
    if (fadt_header->length >= 148 && !dsdt_address) {
        uint64_t extended_dsdt = read_u64(fadt + 140);
        if (extended_dsdt <= 0xFFFFFFFF) dsdt_address = (uint32_t)extended_dsdt;
    }
    if (!dsdt_address || !pm1a_control || pm1a_control > 0xFFFF || pm1_control_length < 2) {
        serial_write("ACPI shutdown: unsupported FADT power-control registers.\n");
        return false;
    }

    uint8_t type_a, type_b;
    if (!find_s5_types((const uint8_t *)dsdt_address, &type_a, &type_b)) {
        serial_write("ACPI shutdown: S5 sleep types not found in DSDT.\n");
        return false;
    }

    if (!(inw((uint16_t)pm1a_control) & 1)) {
        uint32_t smi_command = read_u32(fadt + 48);
        uint8_t acpi_enable = fadt[52];
        if (!smi_command || smi_command > 0xFFFF || !acpi_enable) {
            serial_write("ACPI shutdown: firmware ACPI-enable command unavailable.\n");
            return false;
        }
        outb((uint16_t)smi_command, acpi_enable);
        for (uint32_t i = 0; i < 1000000; i++) {
            if (inw((uint16_t)pm1a_control) & 1) break;
            if (i == 999999) {
                serial_write("ACPI shutdown: timed out enabling ACPI mode.\n");
                return false;
            }
        }
    }

    serial_write("ACPI shutdown: sending S5 power-off request.\n");
    uint16_t sleep_enable = 1u << 13;
    uint16_t sleep_type_mask = 7u << 10;
    uint16_t control = inw((uint16_t)pm1a_control);
    control = (uint16_t)((control & ~(sleep_enable | sleep_type_mask)) | ((type_a & 7) << 10) | sleep_enable);
    outw((uint16_t)pm1a_control, control);

    if (pm1b_control && pm1b_control <= 0xFFFF) {
        control = inw((uint16_t)pm1b_control);
        control = (uint16_t)((control & ~(sleep_enable | sleep_type_mask)) | ((type_b & 7) << 10) | sleep_enable);
        outw((uint16_t)pm1b_control, control);
    }
    return true;
}

void system_shutdown(void) {
    if (!acpi_power_off()) return;

    while (1) {
        __asm__ volatile("cli; hlt");
    }
}

void system_reboot(void) {
    uint8_t temp = 0x02;
    
    // Klavye denetleyicisi giriş tamponu boşalana kadar bekle
    while (temp & 0x02) {
        temp = inb(0x64);
    }
    
    // 0x64 portuna reset komutu (0xFE) göndererek CPU'yu yeniden başlat
    outb(0x64, 0xFE);
    
    serial_write("System is rebooting...\n");
    // Donanım anında reset atmazsa işlemciyi durdur
    while(1) {
        __asm__ volatile("cli; hlt");
    }
}