#include <math.h>

double __attribute__((pcs("aapcs"))) __se_pow(double x, double y)   { return pow(x, y); }
double __attribute__((pcs("aapcs"))) __se_cos(double x)             { return cos(x); }
double __attribute__((pcs("aapcs"))) __se_log(double x)             { return log(x); }
double __attribute__((pcs("aapcs"))) __se_log10(double x)           { return log10(x); }
double __attribute__((pcs("aapcs"))) __se_ceil(double x)            { return ceil(x); }
double __attribute__((pcs("aapcs"))) __se_atan2(double y, double x) { return atan2(y, x); }
double __attribute__((pcs("aapcs"))) __se_fmod(double x, double y)  { return fmod(x, y); }

float __attribute__((pcs("aapcs"))) __se_sinf(float x)             { return sinf(x); }
float __attribute__((pcs("aapcs"))) __se_cosf(float x)             { return cosf(x); }
float __attribute__((pcs("aapcs"))) __se_ceilf(float x)            { return ceilf(x); }

void __attribute__((pcs("aapcs"))) __se_sincosf(float x, float *s, float *c)
{
    *s = sinf(x);
    *c = cosf(x);
}

double __attribute__((pcs("aapcs"))) __log10_finite(double x)           { return log10(x); }
double __attribute__((pcs("aapcs"))) __log_finite(double x)             { return log(x); }
double __attribute__((pcs("aapcs"))) __atan2_finite(double y, double x) { return atan2(y, x); }
double __attribute__((pcs("aapcs"))) __fmod_finite(double x, double y)  { return fmod(x, y); }
