#ifndef KERNEL_MEM_HEAP_H
#define KERNEL_MEM_HEAP_H

#include <stdint.h>
#include <stddef.h>

void heap_init(uint32_t heap_start, uint32_t heap_size);
void* kmalloc(size_t size);
void kfree(void* ptr);

#endif