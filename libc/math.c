#include <math.h>

#define PI 3.14159265358979323846

// Basit bir pow implementasyonu (integervari üsler veya temel yaklaşım için)
double pow(double base, double exp) {
    if (exp == 0.0) return 1.0;
    if (base == 0.0) return 0.0;
    
    // Negatif üsler veya ondalıklı üsler için temel bir approximation veya döngü
    // stb_image genellikle gama düzeltmesi (gamma correction) için pow kullanır (~2.2 vb.)
    double result = 1.0;
    int int_exp = (int)exp;
    
    for (int i = 0; i < (int_exp < 0 ? -int_exp : int_exp); i++) {
        result *= base;
    }
    
    if (int_exp < 0) {
        return 1.0 / result;
    }
    return result;
}

// Karekök için Babil Yöntemi (Babylonian method / Heron's method)
double sqrt(double x) {
    if (x <= 0.0) return 0.0;
    double guess = x / 2.0;
    double prev = 0.0;
    
    for (int i = 0; i < 10; i++) { // 10 iterasyon yeterince hassasiyet verir
        prev = guess;
        guess = (guess + x / guess) / 2.0;
        if (guess == prev) break;
    }
    return guess;
}

double floor(double x) {
    int i = (int)x;
    if (x < 0 && x != (double)i) {
        return (double)(i - 1);
    }
    return (double)i;
}

double ceil(double x) {
    int i = (int)x;
    if (x > 0 && x != (double)i) {
        return (double)(i + 1);
    }
    return (double)i;
}

double ldexp(double x, int exp) {
    // x * (2^exp) hesaplaması
    double factor = 1.0;
    if (exp > 0) {
        for (int i = 0; i < exp; i++) factor *= 2.0;
        return x * factor;
    } else {
        for (int i = 0; i < -exp; i++) factor *= 2.0;
        return x / factor;
    }
}

// Açı normalizasyonu yardımcı fonksiyonu (-PI ile PI aralığına getirir)
static double normalize_angle(double x) {
    while (x > PI) x -= 2.0 * PI;
    while (x < -PI) x += 2.0 * PI;
    return x;
}

// Taylor Serisi ile Sinüs Hesaplama
double sin(double x) {
    x = normalize_angle(x);
    double val = x;
    double numer = x * x * x;
    double denom = 6.0; // 3!
    val -= numer / denom;
    
    numer *= x * x;
    denom *= 20.0; // 5! (6 * 4 * 5)
    val += numer / denom;
    
    numer *= x * x;
    denom *= 42.0; // 7! (120 * 6 * 7)
    val -= numer / denom;
    
    return val;
}

// Taylor Serisi / Özdeşlik ile Kosinüs Hesaplama (cos(x) = sin(x + PI/2))
double cos(double x) {
    return sin(x + (PI / 2.0));
}

float powf(float base, float exp) {
    return (float)pow((double)base, (double)exp);
}

float sqrtf(float x) {
    return (float)sqrt((double)x);
}

float sinf(float x) {
    return (float)sin((double)x);
}

float cosf(float x) {
    return (float)cos((double)x);
}