#ifndef EHCI_H
#define EHCI_H

#include <stdint.h>

// EHCI PCI Sınıf Kodları
#define EHCI_CLASS_CODE 0x0C
#define EHCI_SUBCLASS   0x03
#define EHCI_PROG_IF    0x20 // EHCI Programming Interface

// EHCI Sürücü Fonksiyonları
int ehci_detect_controller(void);
void ehci_init(void);
uint8_t ehci_poll(void);
uint8_t ehci_is_active(void);

#endif