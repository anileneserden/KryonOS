#ifndef KERNEL_CRYPTO_TLS_H
#define KERNEL_CRYPTO_TLS_H

#include <stdint.h>
#include <stddef.h>

// Akış kaynağı türleri (İleride e1000 sürücüsü bağlandığında NIC eklenecek)
typedef enum {
    TLS_SOURCE_MOCK,   // Belleğe gömülü test verisi
    TLS_SOURCE_SERIAL, // Seri port üzerinden gelen veri
    TLS_SOURCE_NIC     // Ağ kartından gelen gerçek paket (Gelecekte)
} TlsSourceType;

typedef struct {
    TlsSourceType type;
    const uint8_t *data; // Verinin bulunduğu bellek adresi
    size_t size;         // Toplam veri boyutu
    size_t offset;       // Okuma imleci (cursor)
} TlsStream;

// TLS Kayıt Başlığı (Record Layer Header - 5 Bayt)
typedef struct {
    uint8_t contentType; // Örn: 0x16 (Handshake), 0x17 (Application Data)
    uint16_t version;    // Örn: 0x0303 (TLS 1.2)
    uint16_t length;     // Payload uzunluğu
} __attribute__((packed)) TlsRecordHeader;

// Fonksiyon Prototipleri
void tls_stream_init(TlsStream *stream, TlsSourceType type, const uint8_t *data, size_t size);
int tls_parse_record(TlsStream *stream, TlsRecordHeader *out_header, uint8_t *out_payload, size_t max_payload_len);

// Mock TLS test fonksiyonu
void run_tls_mock_test(void);

#endif