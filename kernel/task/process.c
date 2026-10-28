#include <kernel/task/process.h>
#include <kernel/mem/heap.h>
#include <kernel/mem/vmm.h>
#include <kernel/serial.h>
#include <kernel/string.h>

#define MAX_PROCESSES 32
#define PROCESS_STACK_SIZE (16 * 1024) // Her süreç için 16 KB Stack

static process_t* process_table[MAX_PROCESSES];
static int next_pid = 1;
static process_t* current_process = 0;

void process_manager_init(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_table[i] = 0;
    }
    next_pid = 1;
    current_process = 0;
    serial_write("[Process] Process manager initialized.\n");
}

process_t* process_create(const char* name, uint32_t entry_point) {
    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i] == 0) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        serial_write("[Process ERROR] Max process limit reached!\n");
        return 0;
    }

    process_t* proc = (process_t*)kmalloc(sizeof(process_t));
    if (!proc) return 0;

    proc->pid = next_pid++;
    
    int i = 0;
    while (name[i] != '\0' && i < 31) {
        proc->name[i] = name[i];
        i++;
    }
    proc->name[i] = '\0';

    proc->state = PROCESS_STATE_READY;
    proc->entry_point = entry_point;
    proc->is_active = true;

    // Süreç için yığın (stack) alanı ayır
    proc->stack_mem = (uint8_t*)kmalloc(PROCESS_STACK_SIZE);
    if (!proc->stack_mem) {
        kfree(proc);
        return 0;
    }

    // Stack başlangıcı (x86 aşağı doğru büyür)
    proc->esp = (uint32_t)(proc->stack_mem + PROCESS_STACK_SIZE);
    proc->ebp = proc->esp;

    // Not: İleride her process için vmm_create_directory() ile ayrı sayfa dizini 
    // atanabilir. Şimdilik mevcut kernel page directory veya temel yapıyı kullanıyoruz.
    proc->page_directory = 0; 

    process_table[slot] = proc;
    serial_write("[Process] Created process: ");
    serial_write(proc->name);
    serial_write("\n");

    return proc;
}

void process_destroy(int pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i] && process_table[i]->pid == pid) {
            process_t* proc = process_table[i];
            if (proc->stack_mem) {
                kfree(proc->stack_mem);
            }
            kfree(proc);
            process_table[i] = 0;
            serial_write("[Process] Destroyed process ID.\n");
            return;
        }
    }
}

process_t* process_get_current(void) {
    return current_process;
}