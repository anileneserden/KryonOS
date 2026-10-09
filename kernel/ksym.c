#include <kernel/ksym.h>
#include <kernel/mem/heap.h>
#include <kernel/drivers/video/fb.h>
#include <kernel/serial.h>
#include <kernel/string.h>

/* Global symbol table mapping exported symbol names to their addresses */
static kernel_symbol_t kernel_symbols[] = {
    /* Memory allocation */
    { "kmalloc",       (void*)kmalloc },
    { "kfree",         (void*)kfree },

    /* Framebuffer / Graphics */
    { "fb_putpixel",   (void*)fb_putpixel },
    { "fb_getpixel",   (void*)fb_getpixel },

    /* Serial output / Logging */
    { "serial_write",  (void*)serial_write },

    /* Null terminator to mark end of table */
    { NULL,            NULL }
};

/**
 * @brief Initializes the kernel symbol table subsytem.
 */
void ksym_init(void) {
    serial_write("[KSYM] Kernel Symbol Table initialized.\n");
}

/**
 * @brief Resolves a symbol name to its actual function memory address.
 * 
 * @param name The name of the kernel symbol to lookup
 * @return void* Pointer to the resolved address, or NULL if not found
 */
void* ksym_resolve(const char* name) {
    if (!name) return NULL;

    for (int i = 0; kernel_symbols[i].name != NULL; i++) {
        if (strcmp(kernel_symbols[i].name, name) == 0) {
            return kernel_symbols[i].address;
        }
    }

    serial_write("[KSYM WARN] Symbol not found: ");
    serial_write(name);
    serial_write("\n");
    return NULL;
}