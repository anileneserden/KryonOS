#ifndef KERNEL_POWER_H
#define KERNEL_POWER_H

#include <kernel/multiboot.h>

void power_init(const multiboot_info_t *mboot);
void system_shutdown(void);
void system_reboot(void);

#endif