#include <kernel/drivers/input/mouse_ps2.h>
#include <kernel/serial.h>
#include <arch/x86/io.h>

#define PS2_DATA_PORT 0x60
#define PS2_STATUS_PORT 0x64
#define PS2_COMMAND_PORT 0x64

static uint8_t mouse_cycle = 0;
static int8_t mouse_bytes[3];
int32_t mouse_x = 400; 
int32_t mouse_y = 300;
uint8_t mouse_buttons = 0;

void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) {
        while (--timeout) {
            if ((inb(PS2_STATUS_PORT) & 1) == 1) return;
        }
    } else {
        while (--timeout) {
            if ((inb(PS2_STATUS_PORT) & 2) == 0) return;
        }
    }
}

void mouse_write(uint8_t data) {
    mouse_wait(1);
    outb(PS2_COMMAND_PORT, 0xD4);
    mouse_wait(1);
    outb(PS2_DATA_PORT, data);
}

uint8_t mouse_read(void) {
    mouse_wait(0);
    return inb(PS2_DATA_PORT);
}

void mouse_handler(void) {
    uint8_t status = inb(PS2_STATUS_PORT);
    if (status & 1) {
        int8_t byte = inb(PS2_DATA_PORT);
        
        switch (mouse_cycle) {
            case 0:
                mouse_bytes[0] = byte;
                if (!(byte & 0x08)) break; 
                mouse_cycle++;
                break;
            case 1:
                mouse_bytes[1] = byte;
                mouse_cycle++;
                break;
            case 2:
                mouse_bytes[2] = byte;
                mouse_cycle = 0;

                mouse_buttons = mouse_bytes[0] & 0x07;
                int8_t dx = mouse_bytes[1];
                int8_t dy = mouse_bytes[2];

                if (dx != 0 || dy != 0) {
                    mouse_x += dx;
                    mouse_y -= dy; 

                    if (mouse_x < 0) mouse_x = 0;
                    if (mouse_x > 799) mouse_x = 799;
                    if (mouse_y < 0) mouse_y = 0;
                    if (mouse_y > 599) mouse_y = 599;
                }
                break;
        }
    }
}

void mouse_init(void) {
    serial_write("PS/2 Fare surucusu baslatiliyor...\n");

    uint8_t status;

    mouse_wait(1);
    outb(PS2_COMMAND_PORT, 0xA8);

    mouse_wait(1);
    outb(PS2_COMMAND_PORT, 0x20);
    mouse_wait(0);
    status = (inb(PS2_DATA_PORT) | 2); 
    
    mouse_wait(1);
    outb(PS2_COMMAND_PORT, 0x60);
    mouse_wait(1);
    outb(PS2_DATA_PORT, status);

    mouse_write(0xF6);
    mouse_read(); 

    mouse_write(0xF4);
    mouse_read(); 

    serial_write("PS/2 Fare basariyla yapilandirildi.\n");
}