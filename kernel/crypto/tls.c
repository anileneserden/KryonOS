#include <kernel/crypto/tls.h>
#include <kernel/serial.h>
#include <kernel/fs/vfs.h> // VFS arayüzünü dahil ediyoruz

void tls_stream_init(TlsStream *stream, TlsSourceType type, const uint8_t *data, size_t size) {
    if (!stream) return;
    stream->type = type;
    stream->data = data;
    stream->size = size;
    stream->offset = 0;
}

// Akıştan güvenli bayt okuma fonksiyonu
static size_t tls_stream_read(TlsStream *stream, uint8_t *dest, size_t len) {
    if (!stream || !stream->data) return 0;
    
    size_t remaining = stream->size - stream->offset;
    size_t to_read = (len < remaining) ? len : remaining;
    
    if (to_read == 0) return 0;

    for (size_t i = 0; i < to_read; i++) {
        dest[i] = stream->data[stream->offset + i];
    }
    stream->offset += to_read;
    return to_read;
}

// TLS Kayıt Katmanını Ayrıştırma
int tls_parse_record(TlsStream *stream, TlsRecordHeader *out_header, uint8_t *out_payload, size_t max_payload_len) {
    uint8_t header_buf[5];
    
    // 1. Önce 5 baytlık TLS Record Header'ı oku
    if (tls_stream_read(stream, header_buf, 5) != 5) {
        return -1; // Yetersiz veri veya akış sonu
    }

    out_header->contentType = header_buf[0];
    out_header->version = (header_buf[1] << 8) | header_buf[2];
    out_header->length = (header_buf[3] << 8) | header_buf[4];

    // 2. Payload uzunluğu kapasitemizi aşıyor mu kontrol et
    if (out_header->length > max_payload_len) {
        return -2; // Buffer taşması koruması
    }

    // 3. Şifreli/Şifresiz payload verisini oku
    if (tls_stream_read(stream, out_payload, out_header->length) != out_header->length) {
        return -3;
    }

    return 0; // Başarılı
}

// Disk Üzerinden (KRYFS / VFS) Dosya Okuyarak Test Eden Fonksiyon
void run_tls_mock_test(void) {
    serial_write("[TLS] Reading encrypted TLS file from disk via VFS...\n");

    uint32_t file_size = 0;
    
    // vfs_read_file doğrudan dosyayı heap'e okuyup adresini ve boyutunu döner
    void *file_data = vfs_read_file("C:/Users/anil/tls_test.enc", &file_size);
    
    if (!file_data || file_size == 0) {
        serial_write("[TLS] ERROR: Could not read 'C:/Users/anil/tls_test.enc' from disk!\n");
        return;
    }

    serial_write("[TLS] File read successfully from disk. Initializing stream...\n");

    // Okunan veriyi TLS Stream yapısına bağlıyoruz
    TlsStream stream;
    tls_stream_init(&stream, TLS_SOURCE_MOCK, (const uint8_t*)file_data, file_size);

    TlsRecordHeader header;
    uint8_t payload_buffer[64];

    int res = tls_parse_record(&stream, &header, payload_buffer, sizeof(payload_buffer));
    if (res == 0) {
        serial_write("[TLS] SUCCESS: TLS Record parsed from disk file!\n");
        if (header.contentType == 0x16) {
            serial_write("[TLS] Content Type: Handshake (0x16)\n");
        }
    } else {
        serial_write("[TLS] ERROR: Failed to parse TLS record from disk contents!\n");
    }
}