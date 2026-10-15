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
static uint8_t keyboard_irq_logged;
static uint8_t mouse_irq_logged;

_Static_assert(sizeof(idt_entry_t) == 8, "IDT entry size");

void keyboard_irq_handler(void) {
    if (!keyboard_irq_logged) {
        keyboard_irq_logged = 1;
        serial_write("[IRQ] First PS/2 keyboard interrupt.\n");
    }
    input_pending = 1;
    outb(0x20, 0x20);
}

void mouse_irq_handler(void) {
    if (!mouse_irq_logged) {
        mouse_irq_logged = 1;
        serial_write("[IRQ] First PS/2 mouse interrupt.\n");
    }
    input_pending = 1;
    outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

__asm__(
    ".global keyboard_irq_stub\n"
    "keyboard_irq_stub:\n"
    "    pusha\n"
    "    cld\n"
    "    call keyboard_irq_handler\n"
    "    popa\n"
    "    iret\n"
    ".global mouse_irq_stub\n"
    "mouse_irq_stub:\n"
    "    pusha\n"
    "    cld\n"
    "    call mouse_irq_handler\n"
    "    popa\n"
    "    iret\n");

extern void keyboard_irq_stub(void);
extern void mouse_irq_stub(void);

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

    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);

    uint16_t code_selector;
    __asm__ volatile("mov %%cs, %0" : "=r"(code_selector));
    idt_set_gate(0x21, keyboard_irq_stub, code_selector);
    idt_set_gate(0x2C, mouse_irq_stub, code_selector);

    idt_pointer_t idt_pointer = {
        .limit = sizeof(idt) - 1,
        .base = (uint32_t)(uintptr_t)idt
    };
    __asm__ volatile("lidt %0" : : "m"(idt_pointer));

    input_pending = 0;
    outb(0x21, 0xF9);
    outb(0xA1, 0xEF);
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
    __asm__ volatile("sti; hlt" ::: "memory");
}