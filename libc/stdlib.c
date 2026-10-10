#include <kernel/mem/heap.h>
#include <stddef.h>

void* malloc(size_t size) {
    return kmalloc(size);
}

void free(void* ptr) {
    kfree(ptr);
}

void* realloc(void* ptr, size_t size) {
    if (!ptr) {
        return kmalloc(size);
    }
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }
    
    void* new_ptr = kmalloc(size);
    if (new_ptr) {
        kfree(ptr);
    }
    return new_ptr;
}