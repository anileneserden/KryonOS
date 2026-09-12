#include <kernel/kef.h>
#include <kernel/fs/vfs.h>
#include <kernel/mem/heap.h>
#include <kernel/serial.h>
#include <kernel/string.h>

bool kef_load_and_run(const char* path) {
    uint32_t file_size = 0;
    uint8_t* file = (uint8_t*)vfs_read_file(path, &file_size);

    if (!file || file_size < sizeof(kef_header_t)) {
        serial_write("KEF: dosya okunamadi veya cok kucuk.\n");
        return false;
    }

    kef_header_t* header = (kef_header_t*)file;
    if (header->magic != KEF_MAGIC ||
        header->version != KEF_VERSION ||
        header->architecture != KEF_ARCH_I386 ||
        header->header_size < sizeof(kef_header_t) ||
        header->header_size > file_size ||
        header->payload_size > file_size - header->header_size ||
        header->entry_offset >= header->payload_size ||
        header->payload_size > KEF_MAX_SIZE) {
        serial_write("KEF: gecersiz veya desteklenmeyen baslik.\n");
        return false;
    }

    uint8_t* payload = (uint8_t*)kmalloc(header->payload_size);
    if (!payload) {
        serial_write("KEF: payload icin bellek ayrilamadi.\n");
        return false;
    }

    memcpy(payload, file + header->header_size, header->payload_size);

    serial_write("KEF: C:/test1.kef yuklendi, entry cagriliyor.\n");
    kef_entry_t entry = (kef_entry_t)(payload + header->entry_offset);
    entry();
    serial_write("KEF: uygulama geri dondu.\n");
    return true;
}