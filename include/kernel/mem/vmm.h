#ifndef KERNEL_MEM_VMM_H
#define KERNEL_MEM_VMM_H

#include <stdint.h>

#define PAGE_PRESENT  0x1
#define PAGE_WRITE    0x2
#define PAGE_USER     0x4

void vmm_init(void);
void vmm_switch_directory(uint32_t* pd);
void vmm_map_page(uint32_t physical_addr, uint32_t virtual_addr, uint32_t flags);

#endif