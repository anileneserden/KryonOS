#ifndef KERNEL_TASK_PROCESS_H
#define KERNEL_TASK_PROCESS_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    PROCESS_STATE_READY,
    PROCESS_STATE_RUNNING,
    PROCESS_STATE_BLOCKED,
    PROCESS_STATE_TERMINATED
} process_state_t;

typedef struct {
    int pid;
    char name[32];
    process_state_t state;
    
    uint32_t* page_directory; // VMM sayfa dizini (CR3 için fiziksel/sanal adres)
    uint32_t esp;             // Stack Pointer
    uint32_t ebp;             // Base Pointer
    
    uint32_t entry_point;     // Başlangıç adresi (örn: 0x400000)
    uint8_t* stack_mem;       // Sürece ayrılan yığın belleği
    
    bool is_active;
} process_t;

void process_manager_init(void);
process_t* process_create(const char* name, uint32_t entry_point);
void process_destroy(int pid);
process_t* process_get_current(void);

#endif