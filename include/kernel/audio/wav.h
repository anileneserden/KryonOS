#ifndef WAV_H
#define WAV_H

#include <stdint.h>
#include <stdbool.h>

// Standart RIFF / WAV Header Yapısı (44 Byte)
typedef struct {
    char     riff_id[4];      // "RIFF"
    uint32_t overall_size;    // Dosya boyutu - 8
    char     wave_id[4];      // "WAVE"
    char     fmt_id[4];       // "fmt "
    uint32_t fmt_length;      // Format verisi boyutu
    uint16_t audio_format;    // 1 = PCM (Sıkıştırılmamış)
    uint16_t num_channels;    // 1 = Mono, 2 = Stereo
    uint32_t sample_rate;     // Örn: 44100, 22050, 11025 Hz
    uint32_t byte_rate;       // sample_rate * num_channels * (bits_per_sample / 8)
    uint16_t block_align;     // num_channels * (bits_per_sample / 8)
    uint16_t bits_per_sample; // 8, 16 bit
    char     data_id[4];      // "data"
    uint32_t data_size;      // Ham ses verisinin bayt cinsinden boyutu
} __attribute__((packed)) wav_header_t;

// Fonksiyon Bildirimleri
bool wav_validate_header(wav_header_t* header);
bool wav_play_file(const char* full_path);

#endif