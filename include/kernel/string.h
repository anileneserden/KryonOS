#ifndef KERNEL_STRING_H
#define KERNEL_STRING_H

#include <stdint.h>
#include <stddef.h>

int strcmp(const char* s1, const char* s2);
size_t strlen(const char* s);
void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* s, int c, size_t n);

#endif