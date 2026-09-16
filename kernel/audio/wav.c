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

    uint8_t* file_buffer = (uint8_t*)vfs_read_file(full_path, &file_size);
    if (!file_buffer || file_size < sizeof(wav_header_t)) {
        serial_write("WAV Hata: Dosya okunamadi veya boyutu yetersiz!\n");
        return false;
    }

    wav_header_t* header = (wav_header_t*)file_buffer;

    if (!wav_validate_header(header)) {
        serial_write("WAV Hata: Header dogrulamasi basarisiz!\n");
        kfree(file_buffer);
        return false;
    }

    serial_write("WAV Bilgi: Format=");
    serial_write_num(header->audio_format);
    serial_write(", Kanal=");
    serial_write_num(header->num_channels);
    serial_write(", SampleRate=");
    serial_write_num(header->sample_rate);
    serial_write(", Bits=");
    serial_write_num(header->bits_per_sample);
    serial_write("\n");

    // Dinamik "data" chunk arama
    uint32_t data_offset = 0;
    for (uint32_t i = 12; i < file_size - 8; i++) {
        if (file_buffer[i] == 'd' && file_buffer[i+1] == 'a' && 
            file_buffer[i+2] == 't' && file_buffer[i+3] == 'a') {
            data_offset = i + 8; 
            break;
        }
    }

    if (data_offset == 0 || data_offset >= file_size) {
        serial_write("WAV Hata: 'data' chunk bulunamadi!\n");
        kfree(file_buffer);
        return false;
    }

    uint32_t pcm_size = file_size - data_offset;
    serial_write("WAV Bilgi: Veri baslangic offseti=");
    serial_write_num(data_offset);
    serial_write(", PCM Boyutu=");
    serial_write_num(pcm_size);
    serial_write("\n");

    uint16_t* pcm_data = (uint16_t*)(file_buffer + data_offset);

    ac97_set_sample_rate(header->sample_rate);
    ac97_play_sound(pcm_data, pcm_size);

    return true;
}