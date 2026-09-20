#include <kernel/kef.h>
#include <kernel/fs/vfs.h>
#include <kernel/mem/heap.h>
#include <kernel/serial.h>
#include <kernel/string.h>
#include <ui/wm.h>

static void kef_print(const char* str) {
    serial_write(str);
}

static int kef_window_create(const char* title, int width, int height) {
    window_t* window = wm_create_window(width, height, title);
    return window != 0;
}

static void kef_install_api(void) {
    volatile kef_api_t* api = (volatile kef_api_t*)KEF_API_ADDRESS;
    api->window_create = kef_window_create;
    api->print = kef_print;
}

bool kef_load_and_run(const char* path) {
    uint32_t file_size = 0;
    uint8_t* file = (uint8_t*)vfs_read_file(path, &file_size);

    if (!file || file_size < sizeof(kef_header_t)) {
        serial_write("KEF: file could not be read or is too small.\n");
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
        serial_write("KEF: invalid or unsupported header.\n");
        return false;
    }

    if (KEF_LOAD_ADDRESS + header->payload_size >= KEF_API_ADDRESS) {
        serial_write("KEF: payload overlaps the API address.\n");
        return false;
    }

    uint8_t* payload = (uint8_t*)KEF_LOAD_ADDRESS;
    memcpy(payload, file + header->header_size, header->payload_size);

    kef_install_api();
    serial_write("KEF: file loaded, calling entry point.\n");
    kef_entry_t entry = (kef_entry_t)(payload + header->entry_offset);
    (void)entry();
    serial_write("KEF: application returned.\n");
    kfree(file);
    return true;
}