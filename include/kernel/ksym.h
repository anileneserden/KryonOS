#ifndef KERNEL_KSYM_H
#define KERNEL_KSYM_H

#include <stdint.h>

/* Structure representing a single exported kernel symbol */
typedef struct {
    const char* name;     /* Name of the function/variable */
    void* address;        /* Pointer to the function/variable in kernel memory */
} kernel_symbol_t;

/* Exported Kernel Symbol API */
void ksym_init(void);
void* ksym_resolve(const char* name);

#endif /* KERNEL_KSYM_H */