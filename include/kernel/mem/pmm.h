#ifndef KERNEL_MEM_PMM_H
#define KERNEL_MEM_PMM_H

#include <stdint.h>
#include <kernel/multiboot.h>

#define PAGE_SIZE 4096

void pmm_init(multiboot_info_t* mboot);
void* pmm_alloc_block(void);
void pmm_free_block(void* b);
uint32_t pmm_get_free_memory(void);
uint32_t pmm_get_total_memory(void);

#endif