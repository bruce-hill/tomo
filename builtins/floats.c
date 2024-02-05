#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <gc.h>
#include <gc/cord.h>
#include <math.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "../SipHash/halfsiphash.h"
#include "array.h"
#include "string.h"
#include "types.h"

public CORD Num__cord(const double *f, bool colorize, const TypeInfo *type) { 
    (void)type;
    CORD c;
    if (colorize) CORD_sprintf(&c, "\x1b[35m%g\x1b[33;2m\x1b[m", *f); 
    else CORD_sprintf(&c, "%g", *f); 
    return c; 
} 

static int32_t Num__compare(const double *x, const double *y, const TypeInfo *type) { 
    (void)type;
    return (*x > *y) - (*x < *y);
} 

static bool Num__equal(const double *x, const double *y, const TypeInfo *type) { 
    (void)type;
    return *x == *y;
} 

public Str_t Num__format(double f, int64_t precision) { 
    int len = snprintf(NULL, 0, "%.*f", (int)precision, f); 
    char *str = GC_MALLOC_ATOMIC(len + 1); 
    snprintf(str, len+1, "%.*f", (int)precision, f); 
    return (Str_t){.data=str, .length=len, .stride=1}; 
}

public Str_t Num__scientific(double f, int64_t precision) { 
    int len = snprintf(NULL, 0, "%.*e", (int)precision, f); 
    char *str = GC_MALLOC_ATOMIC(len + 1); 
    snprintf(str, len+1, "%.*e", (int)precision, f); 
    return (Str_t){.data=str, .length=len, .stride=1}; 
}

public double Num__mod(double num, double modulus) { 
    double result = fmod(num, modulus); 
    return (result < 0) != (modulus < 0) ? result + modulus : result; 
}

public bool Num__isinf(double n) { return isinf(n); }
public bool Num__finite(double n) { return finite(n); }
public bool Num__isnan(double n) { return isnan(n); }

typedef bool (*double_pred_t)(double);
typedef double (*double_unary_fn_t)(double);
typedef double (*double_binary_fn_t)(double, double);

public struct {
    TypeInfo type;
    // Constants:
    double NaN, _2_sqrt_pi, e, half_pi, inf, inverse_half_pi, inverse_pi, ln10, ln2,
           log2e, pi, quarter_pi, sqrt2, sqrt_half, tau;
    // Nullary functions:
    double (*random)(void);
    // Predicates:
    double_pred_t finite, isinf, isnan;
    // Unary functions:
    double_unary_fn_t abs, acos, acosh, asin, asinh, atan, atanh, cbrt, ceil, cos, cosh, erf, erfc,
                      exp, exp10, exp2, expm1, floor, j0, j1, log, log10, log1p, log2, logb,
                      nextdown, nextup, rint, round, roundeven, significand, sin, sinh, sqrt,
                      tan, tanh, tgamma, trunc, y0, y1;
    // Binary functions:
    double_binary_fn_t atan2, copysign, dist, hypot, maxmag, minmag, mod, nextafter, pow, remainder;
    // Odds and ends:
    Str_t (*format)(double f, int64_t precision);
    Str_t (*scientific)(double f, int64_t precision);
} Num_type = {
    .type=(TypeInfo){
        .name="Num",
        .size=sizeof(double),
        .align=alignof(double),
        .tag=CustomInfo,
        .CustomInfo={
            .compare=(void*)Num__compare,
            .equal=(void*)Num__equal,
            .cord=(void*)Num__cord,
        },
    },
    .NaN=NAN, ._2_sqrt_pi=M_2_SQRTPI, .e=M_E, .half_pi=M_PI_2, .inf=1./0., .inverse_half_pi=M_2_PI,
    .inverse_pi=M_1_PI, .ln10=M_LN10, .ln2=M_LN2, .log2e=M_LOG2E, .pi=M_PI, .quarter_pi=M_PI_4,
    .sqrt2=M_SQRT2, .sqrt_half=M_SQRT1_2, .tau=2.*M_PI,
    .random=drand48,
    .finite=Num__finite,
    .isinf=Num__isinf,
    .isnan=Num__isnan,
    .atan2=atan2, .copysign=copysign, .dist=fdim, .hypot=hypot, .maxmag=fmaxmag, .minmag=fminmag,
    .mod=Num__mod, .nextafter=nextafter, .pow=pow, .remainder=remainder,
    .abs=fabs, .acos=acos, .acosh=acosh, .asin=asin, .asinh=asinh, .atan=atan, .atanh=atanh,
    .cbrt=cbrt, .ceil=ceil, .cos=cos, .cosh=cosh, .erf=erf, .erfc=erfc, .exp=exp,
    .exp10=exp10, .exp2=exp2, .expm1=expm1, .floor=floor, .j0=j0, .j1=j1, .log=log,
    .log10=log10, .log1p=log1p, .log2=log2, .logb=logb, .nextdown=nextdown, .nextup=nextup,
    .rint=rint, .round=round, .roundeven=roundeven, .significand=significand, .sin=sin,
    .sinh=sinh, .sqrt=sqrt, .tan=tan, .tanh=tanh, .tgamma=tgamma, .trunc=trunc, .y0=y0, .y1=y1,
    .format=Num__format,
    .scientific=Num__scientific,
};

public CORD Num32__cord(float *f, bool colorize, const TypeInfo *type) { 
    (void)type;
    CORD c;
    if (colorize) CORD_sprintf(&c, "\x1b[35m%g_f32\x1b[m", *f);
    else CORD_sprintf(&c, "%g_f32", *f);
    return c;
}

static int32_t Num32__compare(const float *x, const float *y, const TypeInfo *type) { 
    (void)type;
    return (*x > *y) - (*x < *y);
} 

static bool Num32__equal(const float *x, const float *y, const TypeInfo *type) { 
    (void)type;
    return *x == *y;
} 

public Str_t Num32__format(float f, int64_t precision) { 
    int len = snprintf(NULL, 0, "%.*f", (int)precision, f); 
    char *str = GC_MALLOC_ATOMIC(len + 1); 
    snprintf(str, len+1, "%.*f", (int)precision, f); 
    return (Str_t){.data=str, .length=len, .stride=1}; 
}

public Str_t Num32__scientific(float f, int64_t precision) { 
    int len = snprintf(NULL, 0, "%.*e", (int)precision, f); 
    char *str = GC_MALLOC_ATOMIC(len + 1); 
    snprintf(str, len+1, "%.*e", (int)precision, f); 
    return (Str_t){.data=str, .length=len, .stride=1}; 
}

public float Num32__mod(float num, float modulus) { 
    float result = fmodf(num, modulus); 
    return (result < 0) != (modulus < 0) ? result + modulus : result; 
}

public float Num32__random(void) { 
    return (float)drand48(); 
}

public bool Num32__isinf(float n) { return isinf(n); }
public bool Num32__finite(float n) { return finite(n); }
public bool Num32__isnan(float n) { return isnan(n); }

typedef bool (*float_pred_t)(float);
typedef float (*float_unary_fn_t)(float);
typedef float (*float_binary_fn_t)(float, float);

public struct {
    TypeInfo type;
    // Alphabetized:
    float NaN, _2_sqrt_pi, e, half_pi, inf, inverse_half_pi, inverse_pi, ln10, ln2,
          log2e, pi, quarter_pi, sqrt2, sqrt_half, tau;
    // Nullary functions:
    float (*random)(void);
    // Predicates:
    float_pred_t finite, isinf, isnan;
    // Unary functions:
    float_unary_fn_t abs, acos, acosh, asin, asinh, atan, atanh, cbrt, ceil, cos, cosh, erf, erfc,
                      exp, exp10, exp2, expm1, floor, j0, j1, log, log10, log1p, log2, logb,
                      nextdown, nextup, rint, round, roundeven, significand, sin, sinh, sqrt,
                      tan, tanh, tgamma, trunc, y0, y1;
    // Binary functions:
    float_binary_fn_t atan2, copysign, dist, hypot, maxmag, minmag, mod, nextafter, pow, remainder;
    // Odds and ends:
    Str_t (*format)(float f, int64_t precision);
    Str_t (*scientific)(float f, int64_t precision);
} Num32_type = {
    .type=(TypeInfo){
        .name="Num32",
        .size=sizeof(float),
        .align=alignof(float),
        .tag=CustomInfo,
        .CustomInfo={
            .compare=(void*)Num32__compare,
            .equal=(void*)Num32__equal,
            .cord=(void*)Num32__cord,
        },
    },
    .NaN=NAN, ._2_sqrt_pi=M_2_SQRTPI, .e=M_E, .half_pi=M_PI_2, .inf=1./0., .inverse_half_pi=M_2_PI,
    .inverse_pi=M_1_PI, .ln10=M_LN10, .ln2=M_LN2, .log2e=M_LOG2E, .pi=M_PI, .quarter_pi=M_PI_4,
    .sqrt2=M_SQRT2, .sqrt_half=M_SQRT1_2, .tau=2.*M_PI,
    .random=Num32__random,
    .finite=Num32__finite,
    .isinf=Num32__isinf,
    .isnan=Num32__isnan,
    .atan2=atan2f, .copysign=copysignf, .dist=fdimf, .hypot=hypotf, .maxmag=fmaxmagf, .minmag=fminmagf,
    .mod=Num32__mod, .nextafter=nextafterf, .pow=powf, .remainder=remainderf,
    .abs=fabsf, .acos=acosf, .acosh=acoshf, .asin=asinf, .asinh=asinhf, .atan=atanf, .atanh=atanhf,
    .cbrt=cbrtf, .ceil=ceilf, .cos=cosf, .cosh=coshf, .erf=erff, .erfc=erfcf, .exp=expf,
    .exp10=exp10f, .exp2=exp2f, .expm1=expm1f, .floor=floorf, .j0=j0f, .j1=j1f, .log=logf,
    .log10=log10f, .log1p=log1pf, .log2=log2f, .logb=logbf, .nextdown=nextdownf, .nextup=nextupf,
    .rint=rintf, .round=roundf, .roundeven=roundevenf, .significand=significandf, .sin=sinf,
    .sinh=sinhf, .sqrt=sqrtf, .tan=tanf, .tanh=tanhf, .tgamma=tgammaf, .trunc=truncf, .y0=y0f, .y1=y1f,
    .format=Num32__format,
    .scientific=Num32__scientific,
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
