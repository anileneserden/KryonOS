#ifndef AC97_H
#define AC97_H

#include <stdint.h>

// Intel AC'97 PCI Kimlikleri
#define AC97_VENDOR_ID 0x8086
#define AC97_DEVICE_ID 0x2415

// NABMBAR (Bus Master) Register Offset'leri
#define AC97_PO_BDBAR 0x10 // PCM Out Buffer Descriptor List Base Address
#define AC97_PO_CIV   0x14 // Current Index Value
#define AC97_PO_LVI   0x15 // Last Valid Index
#define AC97_PO_SR    0x16 // Status Register
#define AC97_PO_CR    0x1B // Control Register
#define AC97_GLOB_CNT 0x2C // Global Control Register

// NAMBAR (Mixer) Register Offset'leri
#define AC97_RESET               0x00
#define AC97_MASTER_VOL          0x02
#define AC97_PCM_OUT_VOL         0x18
#define AC97_PCM_FRONT_DAC_RATE  0x2C // Sample Rate Register

// Control Register Bit'leri
#define AC97_CR_RPBM  0x01 // Run/Pause Bus Master
#define AC97_CR_RR    0x02 // Reset Registers

// BDL Entry Yapısı (DMA İçin 8-byte Hizalanmış Yapı)
typedef struct {
    uint32_t ptr;     // Fiziksel Tampon Adresi
    uint16_t samples; // Örnek Sayısı
    uint16_t flags;   // Kontrol Bitleri (IOC=0x8000, BUP=0x4000)
} __attribute__((packed)) ac97_bdl_entry_t;

// Sürücü Durum Yapısı
typedef struct {
    uint16_t nambar;  // Mixer I/O Port Adresi (BAR0)
    uint16_t nabmbar; // Bus Master I/O Port Adresi (BAR1)
    uint8_t  irq;
    uint8_t  found;
} ac97_device_t;

// Nota Yapısı
typedef struct {
    uint32_t freq;     // Frekans (Hz)
    uint32_t duration; // Süre (ms)
} note_t;

// Fonksiyon Bildirimleri
int  ac97_init(void);
void ac97_set_master_volume(uint8_t volume);
void ac97_set_sample_rate(uint32_t hz);
void ac97_play_sound(uint16_t* buffer, uint32_t length);
void ac97_play_tone(uint32_t frequency, uint32_t duration_ms);

#endif