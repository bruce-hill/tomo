// Type infos and methods for Nums (floating point)

#include <float.h>
#include <gc.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "array.h"
#include "nums.h"
#include "string.h"
#include "text.h"
#include "types.h"

public PUREFUNC Text_t Num$as_text(const double *f, bool colorize, const TypeInfo *type) { 
    (void)type;
    if (!f) return Text("Num");
    return Text$format(colorize ? "\x1b[35m%.16g\x1b[33;2m\x1b[m" : "%.16g", *f); 
} 

public PUREFUNC int32_t Num$compare(const double *x, const double *y, const TypeInfo *type) { 
    (void)type;
    return (*x > *y) - (*x < *y);
} 

public PUREFUNC bool Num$equal(const double *x, const double *y, const TypeInfo *type) { 
    (void)type;
    return *x == *y;
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

public Text_t Num$format(double f, Int_t precision) { 
    return Text$format("%.*f", (int)Int_to_Int64(precision, false), f); 
}

public Text_t Num$scientific(double f, Int_t precision) { 
    return Text$format("%.*e", (int)Int_to_Int64(precision, false), f); 
}

public double Num$mod(double num, double modulus) { 
    double result = fmod(num, modulus); 
    return (result < 0) != (modulus < 0) ? result + modulus : result; 
}

public double Num$random(void) { 
    return drand48(); 
}

public CONSTFUNC double Num$mix(double amount, double x, double y) { 
    return (1.0-amount)*x + amount*y;
}

public OptionalNum_t Num$from_text(Text_t text) {
    const char *str = Text$as_c_string(text);
    char *end = NULL;
    double d = strtod(str, &end);
    if (end > str && end[0] == '\0')
        return d;
    else
        return nan("null");
}

public double Num$nan(Text_t tag) {
    return nan(Text$as_c_string(tag));
}

public CONSTFUNC bool Num$isinf(double n) { return !!isinf(n); }
public CONSTFUNC bool Num$finite(double n) { return !!finite(n); }
public CONSTFUNC bool Num$isnan(double n) { return !!isnan(n); }

public const TypeInfo Num$info = {
    .size=sizeof(double),
    .align=__alignof__(double),
    .tag=CustomInfo,
    .CustomInfo={
        .compare=(void*)Num$compare,
        .equal=(void*)Num$equal,
        .as_text=(void*)Num$as_text,
    },
};

public PUREFUNC Text_t Num32$as_text(const float *f, bool colorize, const TypeInfo *type) { 
    (void)type;
    if (!f) return Text("Num32");
    return Text$format(colorize ? "\x1b[35m%.8g_f32\x1b[33;2m\x1b[m" : "%.8g_f32", (double)*f); 
}

public PUREFUNC int32_t Num32$compare(const float *x, const float *y, const TypeInfo *type) { 
    (void)type;
    return (*x > *y) - (*x < *y);
} 

public PUREFUNC bool Num32$equal(const float *x, const float *y, const TypeInfo *type) { 
    (void)type;
    return *x == *y;
}

public CONSTFUNC bool Num32$near(float a, float b, float ratio, float absolute) {
    if (ratio < 0) ratio = 0;
    else if (ratio > 1) ratio = 1;

    if (a == b) return true;

    float diff = fabs(a - b);
    if (diff < absolute) return true;
    else if (isnan(diff)) return false;

    float epsilon = fabs(a * ratio) + fabs(b * ratio);
    if (isinf(epsilon)) epsilon = FLT_MAX;
    return (diff < epsilon);
}

public Text_t Num32$format(float f, Int_t precision) { 
    return Text$format("%.*f", (int)Int_to_Int64(precision, false), (double)f); 
}

public Text_t Num32$scientific(float f, Int_t precision) { 
    return Text$format("%.*e", (int)Int_to_Int64(precision, false), (double)f); 
}

public float Num32$mod(float num, float modulus) { 
    float result = fmodf(num, modulus); 
    return (result < 0) != (modulus < 0) ? result + modulus : result; 
}

public float Num32$random(void) { 
    return (float)drand48(); 
}

public CONSTFUNC float Num32$mix(float amount, float x, float y) { 
    return (1.0f-amount)*x + amount*y;
}

public OptionalNum32_t Num32$from_text(Text_t text) {
    const char *str = Text$as_c_string(text);
    char *end = NULL;
    double d = strtod(str, &end);
    if (end > str && end[0] == '\0')
        return d;
    else
        return nan("null");
}

public float Num32$nan(Text_t tag) {
    return nanf(Text$as_c_string(tag));
}

public CONSTFUNC bool Num32$isinf(float n) { return isinf(n); }
public CONSTFUNC bool Num32$finite(float n) { return finite(n); }
public CONSTFUNC bool Num32$isnan(float n) { return isnan(n); }

public const TypeInfo Num32$info = {
    .size=sizeof(float),
    .align=__alignof__(float),
    .tag=CustomInfo,
    .CustomInfo={
        .compare=(void*)Num32$compare,
        .equal=(void*)Num32$equal,
        .as_text=(void*)Num32$as_text,
    },
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
