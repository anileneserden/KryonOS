#include <kernel/mem/heap.h>
#include <kernel/mem/pmm.h>
#include <kernel/serial.h>

typedef struct block_header {
    uint32_t size;
    uint8_t is_free;
    struct block_header* next;
} block_header_t;

#define HEAP_START_ADDRESS 0x600000 // 6MB adresinden baslatalim (Guvenli bolge)
#define HEAP_INITIAL_SIZE  (1024 * 1024 * 4) // 4MB baslangic boyutu

static block_header_t* heap_head = (block_header_t*)HEAP_START_ADDRESS;

void heap_init(uint32_t heap_start, uint32_t heap_size) {
    heap_head = (block_header_t*)heap_start;
    heap_head->size = heap_size - sizeof(block_header_t); // Sabit yerine gelen parametreyi kullan
    heap_head->is_free = 1;
    heap_head->next = 0;
    serial_write("HEAP: Dinamik bellek yoneticisi (Heap) baslatildi.\n");
}

void* kmalloc(size_t size) {
    // 4 bayt hizalama
    size = (size + 3) & ~3;

    block_header_t* current = heap_head;
    while (current) {
        if (current->is_free && current->size >= size) {
            // Blok fazlasiyla buyukse ikiye bol
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
    serial_write("HEAP HATA: kmalloc bellek tahsis edemedi!\n");
    return 0;
}

void kfree(void* ptr) {
    if (!ptr) return;

    block_header_t* header = (block_header_t*)((uint32_t)ptr - sizeof(block_header_t));
    header->is_free = 1;

    // Bitisik bos bloklari birlestir (Coalescing)
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