#ifndef KERNEL_KEF_H
#define KERNEL_KEF_H

#include <stdint.h>
#include <stdbool.h>

#define KEF_MAGIC 0x0046454B /* "KEF\0" */
#define KEF_VERSION 1
#define KEF_ARCH_I386 1
#define KEF_MAX_SIZE 16384

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t architecture;
    uint32_t entry_offset;
    uint32_t payload_size;
    uint32_t flags;
    uint32_t header_size;
} __attribute__((packed)) kef_header_t;

typedef int (*kef_entry_t)(void);

bool kef_load_and_run(const char* path);

#endif