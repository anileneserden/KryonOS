#ifndef ARCH_X86_IO_H
#define ARCH_X86_IO_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void insl(uint16_t port, void* addr, uint32_t count) {
    __asm__ volatile ("cld; rep insl" : "+D" (addr), "+c" (count) : "d" (port) : "memory");
}

static inline void outsl(uint16_t port, const void* addr, uint32_t count) {
    __asm__ volatile ("cld; rep outsl" : "+S" (addr), "+c" (count) : "d" (port) : "memory");
}

#endif