#ifndef _LIBC_MATH_H
#define _LIBC_MATH_H

#define PI 3.14159265358979323846f

double pow(double base, double exp);
double sqrt(double x);
double floor(double x);
double ceil(double x);
double ldexp(double x, int exp);
double sin(double x);
double cos(double x);

float powf(float base, float exp);
float sqrtf(float x);
float sinf(float x);
float cosf(float x);

#endif