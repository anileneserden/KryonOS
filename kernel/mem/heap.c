#include <kernel/mem/heap.h>
#include <kernel/mem/pmm.h>
#include <kernel/serial.h>

typedef struct block_header {
    uint32_t size;
    uint8_t is_free;
    struct block_header* next;
} block_header_t;

#define HEAP_START_ADDRESS 0x2000000 // Place at 32MB (safely away from the kernel and other structures)
#define HEAP_INITIAL_SIZE  (1024 * 1024 * 4) // Initial size: 4MB

static block_header_t* heap_head = (block_header_t*)HEAP_START_ADDRESS;

void heap_init(uint32_t heap_start, uint32_t heap_size) {
    heap_head = (block_header_t*)heap_start;
    heap_head->size = heap_size - sizeof(block_header_t); // Use the supplied parameter instead of a constant
    heap_head->is_free = 1;
    heap_head->next = 0;
    serial_write("HEAP: Dynamic memory manager initialized.\n");
}

void* kmalloc(size_t size) {
    size = (size + 3) & ~3;

    block_header_t* current = heap_head;
    while (current) {
        if (current->is_free && current->size >= size) {
            if (current->size > size + sizeof(block_header_t) + 4) {
                block_header_t* next_block = (block_header_t*)((uint32_t)current + sizeof(block_header_t) + size);
                next_block->size = current->size - size - sizeof(block_header_t);
                next_block->is_free = 1;
                next_block->next = current->next;

                current->size = size;
                current->next = next_block;
            }
            current->is_free = 0;
            return (void*)((uint32_t)current + sizeof(block_header_t));
        }
        current = current->next;
    }
    serial_write("HEAP ERROR: kmalloc could not allocate memory! (Heap full or corrupted)\n");
    return 0;
}

void kfree(void* ptr) {
    if (!ptr) return;

    block_header_t* header = (block_header_t*)((uint32_t)ptr - sizeof(block_header_t));
    header->is_free = 1;

    // Merge adjacent free blocks (coalescing)
    block_header_t* current = heap_head;
    while (current && current->next) {
        if (current->is_free && current->next->is_free) {
            current->size += sizeof(block_header_t) + current->next->size;
            current->next = current->next->next;
        } else {
            current = current->next;
        }
    }
}