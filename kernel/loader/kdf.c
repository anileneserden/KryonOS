#include <kernel/loader/kdf.h>
#include <kernel/mem/heap.h>
#include <kernel/fs/vfs.h>
#include <kernel/ksym.h>
#include <kernel/serial.h>
#include <kernel/string.h>
#include <arch/x86/io.h>

#define MAX_LOADED_DRIVERS 16

/* Global Kernel API jump table exposed to KDF driver modules */
static kdf_api_t g_kdf_api = {
    .kmalloc      = kmalloc,
    .kfree        = kfree,
    .serial_write = serial_write,
    .outb         = outb,
    .inb          = inb,
    .outw         = outw,
    .inw          = inw,
    .outl         = outl,
    .inl          = inl
};

/* Internal driver table to track active modules */
static kdf_module_t loaded_drivers[MAX_LOADED_DRIVERS];
static uint32_t loaded_driver_count = 0;

/**
 * @brief Loads a KDF (Kryon Driver Format v1) module into kernel memory and executes its entry point.
 * 
 * @param filepath Path to the .kdf file on VFS (e.g., "C:/Kryon/System32/drivers/ac97.kdf")
 * @return int 0 on success, negative value on error
 */
int kdf_load_driver(const char* filepath) {
    if (!filepath) {
        serial_write("[KDF ERROR] Invalid filepath pointer.\n");
        return -1;
    }

    if (loaded_driver_count >= MAX_LOADED_DRIVERS) {
        serial_write("[KDF ERROR] Maximum loaded driver limit reached.\n");
        return -2;
    }

    /* 1. Read driver binary from VFS */
    uint32_t file_size = 0;
    uint8_t* file_buffer = (uint8_t*)vfs_read_file(filepath, &file_size);

    if (!file_buffer || file_size < sizeof(kdf_header_t)) {
        serial_write("[KDF ERROR] Failed to read driver file or file is too small.\n");
        if (file_buffer) kfree(file_buffer);
        return -3;
    }

    /* 2. Validate KDF Header */
    kdf_header_t* header = (kdf_header_t*)file_buffer;
    if (header->magic != KDF_MAGIC) {
        serial_write("[KDF ERROR] Invalid magic number! Not a valid KDF1 driver.\n");
        kfree(file_buffer);
        return -4;
    }

    /* Check for duplicate module loading */
    for (uint32_t i = 0; i < loaded_driver_count; i++) {
        if (loaded_drivers[i].is_active && strcmp(loaded_drivers[i].name, header->name) == 0) {
            serial_write("[KDF WARN] Driver is already loaded: ");
            serial_write(header->name);
            serial_write("\n");
            kfree(file_buffer);
            return 0; // Already loaded
        }
    }

    /* 3. Allocate Kernel Heap Memory for Executable Code */
    uint32_t alloc_size = header->text_size;
    if (alloc_size == 0) {
        alloc_size = file_size - header->text_offset;
    }

    uint8_t* driver_code_exec = (uint8_t*)kmalloc(alloc_size);
    if (!driver_code_exec) {
        serial_write("[KDF ERROR] Out of memory: Cannot allocate driver code buffer.\n");
        kfree(file_buffer);
        return -5;
    }

    /* Copy executable section from file buffer to driver execution buffer */
    memcpy(driver_code_exec, file_buffer + header->text_offset, alloc_size);

    /* 4. Resolve Function Pointers (Init and Cleanup Entries) */
    kdf_init_fn_t init_fn = (kdf_init_fn_t)((uint32_t)driver_code_exec + header->init_entry);
    kdf_cleanup_fn_t cleanup_fn = NULL;
    
    if (header->cleanup_entry != 0) {
        cleanup_fn = (kdf_cleanup_fn_t)((uint32_t)driver_code_exec + header->cleanup_entry);
    }

    /* 5. Register Driver into Kernel Tracking Table */
    kdf_module_t* module = &loaded_drivers[loaded_driver_count];
    memset(module->name, 0, sizeof(module->name));
    strncpy(module->name, header->name, sizeof(module->name) - 1);
    module->base_address = (void*)driver_code_exec;
    module->size = alloc_size;
    module->init = init_fn;
    module->cleanup = cleanup_fn;
    module->is_active = true;

    /* 6. Free Original Raw File Buffer */
    kfree(file_buffer);

    /* 7. Execute Driver Initialization Point with Kernel Jump Table */
    serial_write("[KDF] Initializing driver module: ");
    serial_write(module->name);
    serial_write("...\n");

    int status = init_fn(&g_kdf_api);
    if (status != 0) {
        serial_write("[KDF ERROR] Driver init_module failed with status code.\n");
        kfree(driver_code_exec);
        module->is_active = false;
        return -6;
    }

    loaded_driver_count++;
    serial_write("[KDF SUCCESS] Driver loaded and running successfully!\n");
    return 0;
}

/**
 * @brief Unloads an active KDF module from memory and calls its cleanup routine.
 * 
 * @param driver_name Name of the loaded driver module
 * @return int 0 on success, negative value on error
 */
int kdf_unload_driver(const char* driver_name) {
    if (!driver_name) return -1;

    for (uint32_t i = 0; i < loaded_driver_count; i++) {
        if (loaded_drivers[i].is_active && strcmp(loaded_drivers[i].name, driver_name) == 0) {
            serial_write("[KDF] Unloading driver: ");
            serial_write(driver_name);
            serial_write("...\n");

            /* Call cleanup handler if defined */
            if (loaded_drivers[i].cleanup) {
                loaded_drivers[i].cleanup();
            }

            /* Free allocated executable memory */
            if (loaded_drivers[i].base_address) {
                kfree(loaded_drivers[i].base_address);
            }

            loaded_drivers[i].is_active = false;
            serial_write("[KDF SUCCESS] Driver unloaded.\n");
            return 0;
        }
    }

    serial_write("[KDF WARN] Driver not found for unloading.\n");
    return -2;
}

/**
 * @brief Reads C:/Kryon/System32/drivers/installedDrivers.cfg on boot and auto-loads persistent drivers.
 */
void kdf_init_autoload(void) {
    serial_write("[KDF] Reading persistent drivers from C:/Kryon/System32/drivers/installedDrivers.cfg...\n");

    uint32_t file_size = 0;
    char* cfg_data = (char*)vfs_read_file("C:/Kryon/System32/drivers/installedDrivers.cfg", &file_size);

    if (!cfg_data || file_size == 0) {
        serial_write("[KDF] No persistent driver configuration found.\n");
        if (cfg_data) kfree(cfg_data);
        return;
    }

    char path_buffer[256];
    int buf_idx = 0;

    for (uint32_t i = 0; i < file_size; i++) {
        if (cfg_data[i] == '\r') continue;

        if (cfg_data[i] == '\n' || i == file_size - 1) {
            if (i == file_size - 1 && cfg_data[i] != '\n' && cfg_data[i] != '\r') {
                path_buffer[buf_idx++] = cfg_data[i];
            }

            path_buffer[buf_idx] = '\0';

            if (buf_idx > 0) {
                serial_write("[KDF Autoload] Loading: ");
                serial_write(path_buffer);
                serial_write("\n");

                kdf_load_driver(path_buffer);
            }

            buf_idx = 0;
        } else {
            if (buf_idx < 255) {
                path_buffer[buf_idx++] = cfg_data[i];
            }
        }
    }

    kfree(cfg_data);
}