#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <gc.h>
#include <gc/cord.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "../SipHash/halfsiphash.h"
#include "array.h"
#include "nums.h"
#include "string.h"
#include "types.h"

public CORD Num__as_str(const double *f, bool colorize, const TypeInfo *type) { 
    (void)type;
    if (!f) return "Num";
    CORD c;
    if (colorize) CORD_sprintf(&c, "\x1b[35m%g\x1b[33;2m\x1b[m", *f); 
    else CORD_sprintf(&c, "%g", *f); 
    return c; 
} 

public int32_t Num__compare(const double *x, const double *y, const TypeInfo *type) { 
    (void)type;
    return (*x > *y) - (*x < *y);
} 

public bool Num__equal(const double *x, const double *y, const TypeInfo *type) { 
    (void)type;
    return *x == *y;
} 

public CORD Num__format(double f, int64_t precision) { 
    return CORD_asprintf("%.*f", (int)precision, f);
}

public CORD Num__scientific(double f, int64_t precision) { 
    return CORD_asprintf("%.*e", (int)precision, f); 
}

public double Num__mod(double num, double modulus) { 
    double result = fmod(num, modulus); 
    return (result < 0) != (modulus < 0) ? result + modulus : result; 
}

public double Num__nan(CORD tag) {
    return nan(CORD_to_const_char_star(tag));
}

public bool Num__isinf(double n) { return isinf(n); }
public bool Num__finite(double n) { return finite(n); }
public bool Num__isnan(double n) { return isnan(n); }

public const TypeInfo Num = {
    .size=sizeof(double),
    .align=__alignof__(double),
    .tag=CustomInfo,
    .CustomInfo={
        .compare=(void*)Num__compare,
        .equal=(void*)Num__equal,
        .as_str=(void*)Num__as_str,
    },
};

public CORD Num32__as_str(const float *f, bool colorize, const TypeInfo *type) { 
    (void)type;
    if (!f) return "Num32";
    CORD c;
    if (colorize) CORD_sprintf(&c, "\x1b[35m%g_f32\x1b[m", *f);
    else CORD_sprintf(&c, "%g_f32", *f);
    return c;
}

public int32_t Num32__compare(const float *x, const float *y, const TypeInfo *type) { 
    (void)type;
    return (*x > *y) - (*x < *y);
} 

public bool Num32__equal(const float *x, const float *y, const TypeInfo *type) { 
    (void)type;
    return *x == *y;
} 

public CORD Num32__format(float f, int64_t precision) { 
    return CORD_asprintf("%.*f", (int)precision, f); 
}

public CORD Num32__scientific(float f, int64_t precision) { 
    return CORD_asprintf("%.*e", (int)precision, f); 
}

public float Num32__mod(float num, float modulus) { 
    float result = fmodf(num, modulus); 
    return (result < 0) != (modulus < 0) ? result + modulus : result; 
}

public float Num32__random(void) { 
    return (float)drand48(); 
}

public float Num32__nan(CORD tag) {
    return nanf(CORD_to_const_char_star(tag));
}

public bool Num32__isinf(float n) { return isinf(n); }
public bool Num32__finite(float n) { return finite(n); }
public bool Num32__isnan(float n) { return isnan(n); }

public const TypeInfo Num32 = {
    .size=sizeof(float),
    .align=__alignof__(float),
    .tag=CustomInfo,
    .CustomInfo={
        .compare=(void*)Num32__compare,
        .equal=(void*)Num32__equal,
        .as_str=(void*)Num32__as_str,
    },
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
