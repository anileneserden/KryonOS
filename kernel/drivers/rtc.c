#include <kernel/drivers/rtc.h>
#include <arch/x86/io.h>

#define RTC_ADDRESS_PORT 0x70
#define RTC_DATA_PORT    0x71

// RTC güncelleniyor mu kontrolü (UIP - Update In Progress bit)
static int get_update_in_progress_flag(void) {
    outb(RTC_ADDRESS_PORT, 0x0A);
    return (inb(RTC_DATA_PORT) & 0x80);
}

static unsigned char get_rtc_register(int reg) {
    outb(RTC_ADDRESS_PORT, reg);
    return inb(RTC_DATA_PORT);
}

void rtc_init(void) {
    // RTC başlangıç ayarları (gerekirse kesintiler vs. buraya eklenebilir)
}

void rtc_get_time(rtc_time_t* time) {
    unsigned char last_second;
    unsigned char last_minute;
    unsigned char last_hour;
    unsigned char last_day;
    unsigned char last_month;
    unsigned char last_year;
    unsigned char register_b;

    // Güncelleme sırasında veri okumamak için UIP bitinin sıfırlanmasını bekleyelim
    while (get_update_in_progress_flag());

    time->second = get_rtc_register(0x00);
    time->minute = get_rtc_register(0x02);
    time->hour   = get_rtc_register(0x04);
    time->day    = get_rtc_register(0x07);
    time->month  = get_rtc_register(0x08);
    time->year   = get_rtc_register(0x09);

    // Verilerin tutarlı okunmasını doğrulamak için ikinci bir okuma yapıp karşılaştıralım
    do {
        last_second = time->second;
        last_minute = time->minute;
        last_hour   = time->hour;
        last_day    = time->day;
        last_month  = time->month;
        last_year   = time->year;

        while (get_update_in_progress_flag());

        time->second = get_rtc_register(0x00);
        time->minute = get_rtc_register(0x02);
        time->hour   = get_rtc_register(0x04);
        time->day    = get_rtc_register(0x07);
        time->month  = get_rtc_register(0x08);
        time->year   = get_rtc_register(0x09);
    } while ((last_second != time->second) || (last_minute != time->minute) || (last_hour != time->hour) ||
             (last_day != time->day) || (last_month != time->month) || (last_year != time->year));

    // Register B'yi okuyarak verilerin BCD formatında mı yoksa Binary formatta mı saklandığını öğrenelim
    register_b = get_rtc_register(0x0B);

    // Eğer BCD formatındaysa, normal sayıya dönüştürelim
    if (!(register_b & 0x04)) {
        time->second = (time->second & 0x0F) + ((time->second / 16) * 10);
        time->minute = (time->minute & 0x0F) + ((time->minute / 16) * 10);
        time->hour   = ((time->hour & 0x0F) + (((time->hour & 0x70) / 16) * 10)) | (time->hour & 0x80);
        time->day    = (time->day & 0x0F) + ((time->day / 16) * 10);
        time->month  = (time->month & 0x0F) + ((time->month / 16) * 10);
        time->year   = (time->year & 0x0F) + ((time->year / 16) * 10);
    }

    // 12 saat formatı kontrolü ve 24 saate çevirme
    if (!(register_b & 0x02) && (time->hour & 0x80)) {
        time->hour = ((time->hour & 0x7F) + 12) % 24;
    }

    // Yılı 4 haneli formata tamamlayalım (Örn: 26 -> 2026)
    time->year += 2000;
}