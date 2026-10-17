// kernel/interrupts.c
#include <kernel/interrupts.h>
#include <kernel/serial.h>
#include <arch/x86/io.h>
#include <stdint.h>

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t attributes;
    uint16_t offset_high;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_pointer_t;

static idt_entry_t idt[256];
static volatile uint8_t input_pending;

_Static_assert(sizeof(idt_entry_t) == 8, "IDT entry size");

static void idt_set_gate(uint8_t vector, void (*handler)(void), uint16_t selector) {
    uintptr_t address = (uintptr_t)handler;
    idt[vector].offset_low = (uint16_t)address;
    idt[vector].selector = selector;
    idt[vector].zero = 0;
    idt[vector].attributes = 0x8E;
    idt[vector].offset_high = (uint16_t)(address >> 16);
}

void interrupts_init(void) {
    __asm__ volatile("cli");

    // PIC Remap
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    
    // Başlangıçta tüm PIC interrupt'larını maskele (kapat)
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);

    uint16_t code_selector;
    __asm__ volatile("mov %%cs, %0" : "=r"(code_selector));

    idt_pointer_t idt_pointer = {
        .limit = sizeof(idt) - 1,
        .base = (uint32_t)(uintptr_t)idt
    };
    __asm__ volatile("lidt %0" : : "m"(idt_pointer));

    input_pending = 0;
    
    // PS/2 klavye (IRQ 1) ve fare (IRQ 12) iptal edildi. 
    // Sadece gerekirse sistem saati (IRQ 0 -> 0xFE) açık kalabilir veya tamamen kapatılabilir.
    outb(0x21, 0xFF); // Master PIC: Tüm IRQ'lar maskeli (kapalı)
    outb(0xA1, 0xFF); // Slave PIC: Tüm IRQ'lar maskeli (kapalı)
    
    __asm__ volatile("sti");
}

uint8_t interrupts_take_input_pending(void) {
    __asm__ volatile("cli");
    uint8_t pending = input_pending;
    input_pending = 0;
    __asm__ volatile("sti");
    return pending;
}

void interrupts_wait_for_input(void) {
    __asm__ volatile("cli");
    if (input_pending) {
        __asm__ volatile("sti");
        return;
    }
    // HLt komutu ile CPU'yu xHCI event polling döngüsüne kadar boşa yormadan uyutuyoruz
    __asm__ volatile("sti; hlt" ::: "memory");
}