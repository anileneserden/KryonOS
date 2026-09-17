#ifndef _KERNEL_DRIVERS_USB_UHCI_H
#define _KERNEL_DRIVERS_USB_UHCI_H

#include <stdint.h>

// Transfer Descriptor (TD) - 16 bayt hizalı olmalıdır
typedef struct {
    uint32_t link;       // Sonraki TD'nin adresi (Pointer)
    uint32_t status;     // Durum ve hata bayrakları (Aktif, Hata, Uzunluk vb.)
    uint32_t token;      // Paket türü (SETUP, IN, OUT), Adres ve Endpoint bilgisi
    uint32_t buffer;     // Verinin bulunduğu fiziksel bellek adresi
} __attribute__((packed)) uhci_td_t;

// Queue Head (QH) - Kuyruk Başı Yapısı
typedef struct {
    uint32_t head_link;    // Yatay kuyruk bağlantısı
    uint32_t element_link; // İlk çalıştırılacak TD'nin adresi
} __attribute__((packed)) uhci_qh_t;

void uhci_init(void);

#endif