// Type infos and methods for Nums (floating point)

#include <float.h>
#include <gc.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "fpconv.h"
#include "nums.h"
#include "string.h"
#include "text.h"
#include "types.h"

public PUREFUNC Text_t Num$as_text(const void *f, bool colorize, const TypeInfo_t *info) { 
    (void)info;
    if (!f) return Text("Num");
    char *str = GC_MALLOC_ATOMIC(24);
    int len = fpconv_dtoa(*(double*)f, str);
    static const Text_t color_prefix = Text("\x1b[35m"), color_suffix = Text("\x1b[m");
    Text_t text = Text$from_strn(str, (size_t)len);
    return colorize ? Texts(color_prefix, text, color_suffix) : text;
} 

public PUREFUNC int32_t Num$compare(const void *x, const void *y, const TypeInfo_t *info) { 
    (void)info;
    int64_t rx = *(int64_t*)x,
            ry = *(int64_t*)y;

    if (rx == ry) return 0;

    if (rx < 0) rx ^= INT64_MAX;
    if (ry < 0) ry ^= INT64_MAX;

    return (rx > ry) - (rx < ry);
} 

public PUREFUNC bool Num$equal(const void *x, const void *y, const TypeInfo_t *info) { 
    (void)info;
    return *(double*)x == *(double*)y;
} 

public CONSTFUNC bool Num$near(double a, double b, double ratio, double absolute) {
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

public Text_t Num$percent(double f, double precision) { 
    double d = 100. * f;
    d = Num$with_precision(d, precision);
    return Texts(Num$as_text(&d, false, &Num$info), Text("%"));
}

public CONSTFUNC double Num$with_precision(double num, double precision) {
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

public CONSTFUNC double Num$mod(double num, double modulus) { 
    // Euclidean division, see: https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/divmodnote-letter.pdf
    double r = remainder(num, modulus);
    r -= (r < 0) * (2*(modulus < 0) - 1) * modulus;
    return r;
}

public CONSTFUNC double Num$mod1(double num, double modulus) { 
    return 1.0 + Num$mod(num-1, modulus);
}

public CONSTFUNC double Num$mix(double amount, double x, double y) { 
    return (1.0-amount)*x + amount*y;
}

public CONSTFUNC bool Num$is_between(const double x, const double low, const double high) {
    return low <= x && x <= high;
}
public CONSTFUNC double Num$clamped(double x, double low, double high) {
    return (x <= low) ? low : (x >= high ? high : x);
}

public OptionalNum_t Num$parse(Text_t text, Text_t *remainder) {
    const char *str = Text$as_c_string(text);
    char *end = NULL;
    double d = strtod(str, &end);
    if (end > str) {
        if (remainder)
            *remainder = Text$from_str(end);
        else if (*end != '\0')
            return nan("none");
        return d;
    } else {
        if (remainder) *remainder = text;
        return nan("none");
    }
}

public CONSTFUNC bool Num$is_none(const void *n, const TypeInfo_t *info)
{
    (void)info;
    return isnan(*(Num_t*)n);
}

public CONSTFUNC bool Num$isinf(double n) { return (fpclassify(n) == FP_INFINITE); }
public CONSTFUNC bool Num$finite(double n) { return (fpclassify(n) != FP_INFINITE); }
public CONSTFUNC bool Num$isnan(double n) { return (fpclassify(n) == FP_NAN); }

public const TypeInfo_t Num$info = {
    .size=sizeof(double),
    .align=__alignof__(double),
    .metamethods={
        .compare=Num$compare,
        .equal=Num$equal,
        .as_text=Num$as_text,
        .is_none=Num$is_none,
    },
};

public PUREFUNC Text_t Num32$as_text(const void *f, bool colorize, const TypeInfo_t *info) { 
    (void)info;
    if (!f) return Text("Num32");
    double d = (double)(*(float*)f);
    return Num$as_text(&d, colorize, &Num$info);
}

public PUREFUNC int32_t Num32$compare(const void *x, const void *y, const TypeInfo_t *info) { 
    (void)info;
    return (*(float*)x > *(float*)y) - (*(float*)x < *(float*)y);
} 

public PUREFUNC bool Num32$equal(const void *x, const void *y, const TypeInfo_t *info) { 
    (void)info;
    return *(float*)x == *(float*)y;
}

public CONSTFUNC bool Num32$near(float a, float b, float ratio, float absolute) {
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

public Text_t Num32$percent(float f, float precision) { 
    double d = 100. * (double)f;
    d = Num$with_precision(d, (double)precision);
    return Texts(Num$as_text(&d, false, &Num$info), Text("%"));
}

public CONSTFUNC float Num32$with_precision(float num, float precision) {
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

public CONSTFUNC float Num32$mod(float num, float modulus) { 
    // Euclidean division, see: https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/divmodnote-letter.pdf
    float r = remainderf(num, modulus);
    r -= (r < 0) * (2*(modulus < 0) - 1) * modulus;
    return r;
}

public CONSTFUNC float Num32$mod1(float num, float modulus) { 
    return 1.0f + Num32$mod(num-1, modulus);
}

public CONSTFUNC float Num32$mix(float amount, float x, float y) { 
    return (1.0f-amount)*x + amount*y;
}

public CONSTFUNC bool Num32$is_between(const float x, const float low, const float high) {
    return low <= x && x <= high;
}

public CONSTFUNC float Num32$clamped(float x, float low, float high) {
    return (x <= low) ? low : (x >= high ? high : x);
}

public OptionalNum32_t Num32$parse(Text_t text, Text_t *remainder) {
    const char *str = Text$as_c_string(text);
    char *end = NULL;
    double d = strtod(str, &end);
    if (end > str && end[0] == '\0') {
        if (remainder) *remainder = Text$from_str(end);
        else if (*end != '\0')
            return nan("none");
        return d;
    } else {
        if (remainder) *remainder = text;
        return nan("none");
    }
}

public CONSTFUNC bool Num32$is_none(const void *n, const TypeInfo_t *info)
{
    (void)info;
    return isnan(*(Num32_t*)n);
}

public CONSTFUNC bool Num32$isinf(float n) { return (fpclassify(n) == FP_INFINITE); }
public CONSTFUNC bool Num32$finite(float n) { return (fpclassify(n) != FP_INFINITE); }
public CONSTFUNC bool Num32$isnan(float n) { return (fpclassify(n) == FP_NAN); }

public const TypeInfo_t Num32$info = {
    .size=sizeof(float),
    .align=__alignof__(float),
    .metamethods={
        .compare=Num32$compare,
        .equal=Num32$equal,
        .as_text=Num32$as_text,
        .is_none=Num32$is_none,
    },
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
