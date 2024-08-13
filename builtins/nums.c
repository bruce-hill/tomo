// Type infos and methods for Nums (floating point)

#include <float.h>
#include <gc.h>
#include <gc/cord.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "array.h"
#include "nums.h"
#include "string.h"
#include "types.h"

public CORD Num$as_text(const double *f, bool colorize, const TypeInfo *type) { 
    (void)type;
    if (!f) return "Num";
    CORD c;
    if (colorize) CORD_sprintf(&c, "\x1b[35m%g\x1b[33;2m\x1b[m", *f); 
    else CORD_sprintf(&c, "%g", *f); 
    return c; 
} 

public int32_t Num$compare(const double *x, const double *y, const TypeInfo *type) { 
    (void)type;
    return (*x > *y) - (*x < *y);
} 

public bool Num$equal(const double *x, const double *y, const TypeInfo *type) { 
    (void)type;
    return *x == *y;
} 

public bool Num$near(double a, double b, double ratio, double absolute) {
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

public CORD Num$format(double f, Int_t precision) { 
    return CORD_asprintf("%.*f", (int)Int$as_i64(precision), f);
}

public CORD Num$scientific(double f, Int_t precision) { 
    return CORD_asprintf("%.*e", (int)Int$as_i64(precision), f); 
}

public double Num$mod(double num, double modulus) { 
    double result = fmod(num, modulus); 
    return (result < 0) != (modulus < 0) ? result + modulus : result; 
}

public double Num$random(void) { 
    return drand48(); 
}

public double Num$mix(double amount, double x, double y) { 
    return (1.0-amount)*x + amount*y;
}

public double Num$from_text(CORD text, CORD *the_rest) {
    const char *str = CORD_to_const_char_star(text);
    char *end = NULL;
    double d = strtod(str, &end);
    if (the_rest) *the_rest = CORD_from_char_star(end);
    return d;
}

public double Num$nan(CORD tag) {
    return nan(CORD_to_const_char_star(tag));
}

public bool Num$isinf(double n) { return !!isinf(n); }
public bool Num$finite(double n) { return !!finite(n); }
public bool Num$isnan(double n) { return !!isnan(n); }

public const TypeInfo $Num = {
    .size=sizeof(double),
    .align=__alignof__(double),
    .tag=CustomInfo,
    .CustomInfo={
        .compare=(void*)Num$compare,
        .equal=(void*)Num$equal,
        .as_text=(void*)Num$as_text,
    },
};

public CORD Num32$as_text(const float *f, bool colorize, const TypeInfo *type) { 
    (void)type;
    if (!f) return "Num32";
    CORD c;
    if (colorize) CORD_sprintf(&c, "\x1b[35m%g_f32\x1b[m", *f);
    else CORD_sprintf(&c, "%g_f32", *f);
    return c;
}

public int32_t Num32$compare(const float *x, const float *y, const TypeInfo *type) { 
    (void)type;
    return (*x > *y) - (*x < *y);
} 

public bool Num32$equal(const float *x, const float *y, const TypeInfo *type) { 
    (void)type;
    return *x == *y;
}

public bool Num32$near(float a, float b, float ratio, float absolute) {
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

public CORD Num32$format(float f, Int_t precision) { 
    return CORD_asprintf("%.*f", (int)Int$as_i64(precision), f); 
}

public CORD Num32$scientific(float f, Int_t precision) { 
    return CORD_asprintf("%.*e", (int)Int$as_i64(precision), f); 
}

public float Num32$mod(float num, float modulus) { 
    float result = fmodf(num, modulus); 
    return (result < 0) != (modulus < 0) ? result + modulus : result; 
}

public float Num32$random(void) { 
    return (float)drand48(); 
}

public float Num32$mix(float amount, float x, float y) { 
    return (1.0-amount)*x + amount*y;
}

public float Num32$from_text(CORD text, CORD *the_rest) {
    const char *str = CORD_to_const_char_star(text);
    char *end = NULL;
    double d = strtod(str, &end);
    if (the_rest) *the_rest = CORD_from_char_star(end);
    return (float)d;
}

public float Num32$nan(CORD tag) {
    return nanf(CORD_to_const_char_star(tag));
}

public bool Num32$isinf(float n) { return isinf(n); }
public bool Num32$finite(float n) { return finite(n); }
public bool Num32$isnan(float n) { return isnan(n); }

public const TypeInfo $Num32 = {
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
