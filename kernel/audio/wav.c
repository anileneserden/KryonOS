#include <kernel/audio/wav.h>
#include <kernel/drivers/audio/ac97.h>
#include <kernel/fs/vfs.h>
#include <kernel/mem/heap.h>
#include <kernel/serial.h>
#include <kernel/string.h>

// Sayısal değerleri seri porta kolayca basmak için yardımcı fonksiyon
static void serial_write_num(uint32_t num) {
    char buf[12];
    int i = 10;
    buf[11] = '\0';

    if (num == 0) {
        serial_write("0");
        return;
    }

    while (num > 0 && i >= 0) {
        buf[i--] = '0' + (num % 10);
        num /= 10;
    }
    serial_write(&buf[i + 1]);
}

bool wav_validate_header(wav_header_t* header) {
    if (!header) return false;

    if (memcmp(header->riff_id, "RIFF", 4) != 0 ||
        memcmp(header->wave_id, "WAVE", 4) != 0 ||
        memcmp(header->fmt_id, "fmt ", 4) != 0) {
        serial_write("WAV Hata: Gecersiz RIFF/WAVE baslik imzasi!\n");
        return false;
    }
    
    if (header->audio_format != 1) {
        serial_write("WAV Hata: Sadece uncompressed PCM format destekleniyor!\n");
        return false;
    }

    return true;
}

bool wav_play_file(const char* full_path) {
    uint32_t file_size = 0;

    // VFS üzerinden dosyanın tamamını belleğe yüklüyoruz
    uint8_t* file_buffer = (uint8_t*)vfs_read_file(full_path, &file_size);
    if (!file_buffer || file_size < sizeof(wav_header_t)) {
        serial_write("WAV Hata: Dosya okunamadi veya boyutu yetersiz!\n");
        return false;
    }

    wav_header_t* header = (wav_header_t*)file_buffer;

    if (!wav_validate_header(header)) {
        kfree(file_buffer);
        return false;
    }

    // Dinamik Sample Rate Bilgisi Basma
    serial_write("WAV: Dosya basariyla dogrulandi. Sample Rate: ");
    serial_write_num(header->sample_rate);
    serial_write(" Hz, Kanal: ");
    serial_write_num(header->num_channels);
    serial_write(", Bit: ");
    serial_write_num(header->bits_per_sample);
    serial_write("\n");

    // "data" chunk'ını arayarak dinamik offset bulma
    // Standart header 44 bayttır ancak metadata içeren dosyalarda offset değişebilir
    uint32_t data_offset = 12;
    uint32_t pcm_size = 0;

    while (data_offset < file_size - 8) {
        if (memcmp(file_buffer + data_offset, "data", 4) == 0) {
            pcm_size = *(uint32_t*)(file_buffer + data_offset + 4);
            data_offset += 8; // "data" id (4) + data_size (4)
            break;
        }
        // Bir sonraki chunk'a geç (Chunk ID: 4 bayt, Size: 4 bayt + Size kadar veri)
        uint32_t chunk_size = *(uint32_t*)(file_buffer + data_offset + 4);
        data_offset += 8 + chunk_size;
    }

    // "data" chunk bulunamadıysa fallback olarak standart header boyutunu al
    if (pcm_size == 0 || data_offset >= file_size) {
        data_offset = sizeof(wav_header_t);
        pcm_size = header->data_size;
    }

    uint16_t* pcm_data = (uint16_t*)(file_buffer + data_offset);

    // AC'97 kartına frekansı ayarla ve sesi başlat
    ac97_set_sample_rate(header->sample_rate);
    ac97_play_sound(pcm_data, pcm_size);

    return true;
}