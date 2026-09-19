#ifndef HEXDUMP_H
#define HEXDUMP_H

#include <stdint.h>
#include <stddef.h>

void kernel_hexdump(const void* data, size_t size);

#endif