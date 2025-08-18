// Type infos and methods for Nums (floating point)

#include <float.h>
#include <gc.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "fpconv.h"
#include "lists.h"
#include "nums.h"
#include "string.h"
#include "text.h"
#include "types.h"

public PUREFUNC Text_t Numヽas_text(const void *f, bool colorize, const TypeInfo_t *info) { 
    (void)info;
    if (!f) return Text("Num");
    char *str = GC_MALLOC_ATOMIC(24);
    int len = fpconv_dtoa(*(double*)f, str);
    static const Text_t color_prefix = Text("\x1b[35m"), color_suffix = Text("\x1b[m");
    Text_t text = Textヽfrom_strn(str, (size_t)len);
    return colorize ? Texts(color_prefix, text, color_suffix) : text;
} 

public PUREFUNC int32_t Numヽcompare(const void *x, const void *y, const TypeInfo_t *info) { 
    (void)info;
    int64_t rx = *(int64_t*)x,
            ry = *(int64_t*)y;

    if (rx == ry) return 0;

    if (rx < 0) rx ^= INT64_MAX;
    if (ry < 0) ry ^= INT64_MAX;

    return (rx > ry) - (rx < ry);
} 

public PUREFUNC bool Numヽequal(const void *x, const void *y, const TypeInfo_t *info) { 
    (void)info;
    return *(double*)x == *(double*)y;
} 

public CONSTFUNC bool Numヽnear(double a, double b, double ratio, double absolute) {
    if (ratio < 0) ratio = 0;
    else if (ratio > 1) ratio = 1;

    if (a == b) return true;

    double diff = fabs(a - b);
    if (diff < absolute) return true;
    else if (isnan(diff)) return false;

    double epsilon = fabs(a * ratio) + fabs(b * ratio);
    if (isinf(epsilon)) epsilon = DBL_MAX;
    return (diff < epsilon);
}

public Text_t Numヽpercent(double f, double precision) { 
    double d = 100. * f;
    d = Numヽwith_precision(d, precision);
    return Texts(Numヽas_text(&d, false, &Numヽinfo), Text("%"));
}

public CONSTFUNC double Numヽwith_precision(double num, double precision) {
    if (precision == 0.0) return num;
    // Precision will be, e.g. 0.01 or 100.
    if (precision < 1.) {
        double inv = round(1./precision); // Necessary to make the math work
        double k = num * inv;
        return round(k) / inv;
    } else {
        double k = num / precision;
        return round(k) * precision;
    }
}

public CONSTFUNC double Numヽmod(double num, double modulus) { 
    // Euclidean division, see: https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/divmodnote-letter.pdf
    double r = remainder(num, modulus);
    r -= (r < 0) * (2*(modulus < 0) - 1) * modulus;
    return r;
}

public CONSTFUNC double Numヽmod1(double num, double modulus) { 
    return 1.0 + Numヽmod(num-1, modulus);
}

public CONSTFUNC double Numヽmix(double amount, double x, double y) { 
    return (1.0-amount)*x + amount*y;
}

public CONSTFUNC bool Numヽis_between(const double x, const double low, const double high) {
    return low <= x && x <= high;
}
public CONSTFUNC double Numヽclamped(double x, double low, double high) {
    return (x <= low) ? low : (x >= high ? high : x);
}

public OptionalNum_t Numヽparse(Text_t text, Text_t *remainder) {
    const char *str = Textヽas_c_string(text);
    char *end = NULL;
    double d = strtod(str, &end);
    if (end > str) {
        if (remainder)
            *remainder = Textヽfrom_str(end);
        else if (*end != '\0')
            return nan("none");
        return d;
    } else {
        if (remainder) *remainder = text;
        return nan("none");
    }
}

static bool Numヽis_none(const void *n, const TypeInfo_t *info)
{
    (void)info;
    return isnan(*(Num_t*)n);
}

public CONSTFUNC bool Numヽisinf(double n) { return (fpclassify(n) == FP_INFINITE); }
public CONSTFUNC bool Numヽfinite(double n) { return (fpclassify(n) != FP_INFINITE); }
public CONSTFUNC bool Numヽisnan(double n) { return (fpclassify(n) == FP_NAN); }

public const TypeInfo_t Numヽinfo = {
    .size=sizeof(double),
    .align=__alignof__(double),
    .metamethods={
        .compare=Numヽcompare,
        .equal=Numヽequal,
        .as_text=Numヽas_text,
        .is_none=Numヽis_none,
    },
};

public PUREFUNC Text_t Num32ヽas_text(const void *f, bool colorize, const TypeInfo_t *info) { 
    (void)info;
    if (!f) return Text("Num32");
    double d = (double)(*(float*)f);
    return Numヽas_text(&d, colorize, &Numヽinfo);
}

public PUREFUNC int32_t Num32ヽcompare(const void *x, const void *y, const TypeInfo_t *info) { 
    (void)info;
    return (*(float*)x > *(float*)y) - (*(float*)x < *(float*)y);
} 

public PUREFUNC bool Num32ヽequal(const void *x, const void *y, const TypeInfo_t *info) { 
    (void)info;
    return *(float*)x == *(float*)y;
}

public CONSTFUNC bool Num32ヽnear(float a, float b, float ratio, float absolute) {
    if (ratio < 0) ratio = 0;
    else if (ratio > 1) ratio = 1;

    if (a == b) return true;

    float diff = fabsf(a - b);
    if (diff < absolute) return true;
    else if (isnan(diff)) return false;

    float epsilon = fabsf(a * ratio) + fabsf(b * ratio);
    if (isinf(epsilon)) epsilon = FLT_MAX;
    return (diff < epsilon);
}

public Text_t Num32ヽpercent(float f, float precision) { 
    double d = 100. * (double)f;
    d = Numヽwith_precision(d, (double)precision);
    return Texts(Numヽas_text(&d, false, &Numヽinfo), Text("%"));
}

public CONSTFUNC float Num32ヽwith_precision(float num, float precision) {
    if (precision == 0.0f) return num;
    // Precision will be, e.g. 0.01 or 100.
    if (precision < 1.f) {
        float inv = roundf(1.f/precision); // Necessary to make the math work
        float k = num * inv;
        return roundf(k) / inv;
    } else {
        float k = num / precision;
        return roundf(k) * precision;
    }
}

public CONSTFUNC float Num32ヽmod(float num, float modulus) { 
    // Euclidean division, see: https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/divmodnote-letter.pdf
    float r = remainderf(num, modulus);
    r -= (r < 0) * (2*(modulus < 0) - 1) * modulus;
    return r;
}

public CONSTFUNC float Num32ヽmod1(float num, float modulus) { 
    return 1.0f + Num32ヽmod(num-1, modulus);
}

public CONSTFUNC float Num32ヽmix(float amount, float x, float y) { 
    return (1.0f-amount)*x + amount*y;
}

public CONSTFUNC bool Num32ヽis_between(const float x, const float low, const float high) {
    return low <= x && x <= high;
}

public CONSTFUNC float Num32ヽclamped(float x, float low, float high) {
    return (x <= low) ? low : (x >= high ? high : x);
}

public OptionalNum32_t Num32ヽparse(Text_t text, Text_t *remainder) {
    const char *str = Textヽas_c_string(text);
    char *end = NULL;
    double d = strtod(str, &end);
    if (end > str && end[0] == '\0') {
        if (remainder) *remainder = Textヽfrom_str(end);
        else if (*end != '\0')
            return nan("none");
        return d;
    } else {
        if (remainder) *remainder = text;
        return nan("none");
    }
}

static bool Num32ヽis_none(const void *n, const TypeInfo_t *info)
{
    (void)info;
    return isnan(*(Num32_t*)n);
}

public CONSTFUNC bool Num32ヽisinf(float n) { return (fpclassify(n) == FP_INFINITE); }
public CONSTFUNC bool Num32ヽfinite(float n) { return (fpclassify(n) != FP_INFINITE); }
public CONSTFUNC bool Num32ヽisnan(float n) { return (fpclassify(n) == FP_NAN); }

public const TypeInfo_t Num32ヽinfo = {
    .size=sizeof(float),
    .align=__alignof__(float),
    .metamethods={
        .compare=Num32ヽcompare,
        .equal=Num32ヽequal,
        .as_text=Num32ヽas_text,
        .is_none=Num32ヽis_none,
    },
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
