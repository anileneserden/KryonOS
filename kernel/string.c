#include <kernel/string.h>
#include <stdarg.h>
#include <stdbool.h>

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, size_t n) {
    if (n == 0) return 0;
    while (n-- > 0 && *s1 && (*s1 == *s2)) {
        if (n == 0 || *s1 == '\0') break;
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;

    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] - p2[i];
        }
    }
    return 0;
}

size_t strlen(const char* s) {
    size_t len = 0;
    while (s[len]) {
        len++;
    }
    return len;
}

void* memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

void* memset(void* s, int c, size_t n) {
    uint8_t* p = (uint8_t*)s;
    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }
    return s;
}

char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

char* strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

char* strcat(char* dest, const char* src) {
    char* d = dest;
    while (*d) {
        d++;
    }
    while ((*d++ = *src++));
    return dest;
}

/* --- KERNEL SNPRINTF IMPLEMENTASYONU --- */

static void itoa_simple(int value, char *str, int base) {
    char temp[32];
    int i = 0;
    bool is_negative = false;

    if (value == 0) {
        str[0] = '0';
        str[1] = '\0';
        return;
    }

    if (value < 0 && base == 10) {
        is_negative = true;
        value = -value;
    }

    while (value != 0) {
        int rem = value % base;
        temp[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        value /= base;
    }

    if (is_negative) {
        temp[i++] = '-';
    }

    int j = 0;
    while (i > 0) {
        str[j++] = temp[--i];
    }
    str[j] = '\0';
}

int vsnprintf(char *str, size_t size, const char *format, va_list args) {
    if (!str || size == 0) return 0;

    size_t pos = 0;
    const char *p = format;

    while (*p != '\0' && pos < size - 1) {
        if (*p != '%') {
            str[pos++] = *p++;
            continue;
        }

        p++; // '%' işaretini geç
        if (*p == '\0') break;

        if (*p == 's') {
            const char *s = va_arg(args, const char*);
            if (!s) s = "(null)";
            while (*s && pos < size - 1) {
                str[pos++] = *s++;
            }
        } else if (*p == 'd' || *p == 'i') {
            int num = va_arg(args, int);
            char num_buf[32];
            itoa_simple(num, num_buf, 10);
            for (char *c = num_buf; *c && pos < size - 1; c++) {
                str[pos++] = *c;
            }
        } else if (*p == 'x' || *p == 'X') {
            int num = va_arg(args, int);
            char num_buf[32];
            itoa_simple(num, num_buf, 16);
            for (char *c = num_buf; *c && pos < size - 1; c++) {
                str[pos++] = *c;
            }
        } else if (*p == 'c') {
            char c = (char)va_arg(args, int);
            if (pos < size - 1) {
                str[pos++] = c;
            }
        } else if (*p == '%') {
            if (pos < size - 1) {
                str[pos++] = '%';
            }
        }

        p++; // İşlenen belirtecin bir sonraki karakterine geç
    }

    str[pos] = '\0';
    return (int)pos;
}

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(str, size, format, args);
    va_end(args);
    return ret;
}