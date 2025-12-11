// Fixed-width integer type infos and methods
// This file is intended to be used by defining `INTX_C_H__INT_BITS` before including:
//
//    #define INTX_C_H__INT_BITS 32
//    #include "intX.c.h"
//

#include <gc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "datatypes.h"
#include "integers.h"
#include "text.h"
#include "types.h"

#ifndef INTX_C_H__INT_BITS
#define INTX_C_H__INT_BITS 32
#endif

#define CAT_(a, b) a##b
#define CAT(a, b) CAT_(a, b)
#define STRINGIFY_(s) #s
#define STRINGIFY(s) STRINGIFY_(s)

#define INT_T CAT(CAT(int, INTX_C_H__INT_BITS), _t)
#define UINT_T CAT(CAT(uint, INTX_C_H__INT_BITS), _t)
#define OPT_T CAT(CAT(OptionalInt, INTX_C_H__INT_BITS), _t)

#define NAME_STR "Int" STRINGIFY(INTX_C_H__INT_BITS)

#define NAMESPACED(method_name) CAT(CAT(Int, INTX_C_H__INT_BITS), CAT($, method_name))

static Text_t _int64_to_text(int64_t n) {
    if (n == INT64_MIN) return Text("-9223372036854775808");

    char buf[21] = {[20] = 0}; // Big enough for INT64_MIN + '\0'
    char *p = &buf[19];
    bool negative = n < 0;
    if (negative) n = -n; // Safe to do because we checked for INT64_MIN earlier

    do {
        *(p--) = '0' + (n % 10);
        n /= 10;
    } while (n > 0);

    if (negative) *(p--) = '-';

    return Text$from_strn(p + 1, (size_t)(&buf[19] - p));
}

public
void NAMESPACED(serialize)(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *info) {
    (void)info, (void)pointers;
#if INTX_C_H__INT_BITS < 32
    fwrite(obj, sizeof(INT_T), 1, out);
#else
    INT_T i = *(INT_T *)obj;
    UINT_T z = (UINT_T)((i << 1L) ^ (i >> (INTX_C_H__INT_BITS - 1L))); // Zigzag encode
    while (z >= 0x80L) {
        fputc((uint8_t)(z | 0x80L), out);
        z >>= 7L;
    }
    fputc((uint8_t)z, out);
#endif
}

public
void NAMESPACED(deserialize)(FILE *in, void *outval, List_t *pointers, const TypeInfo_t *info) {
    (void)info, (void)pointers;
#if INTX_C_H__INT_BITS < 32
    fread(outval, sizeof(INT_T), 1, in);
#else
    UINT_T z = 0;
    for (size_t shift = 0;; shift += 7) {
        uint8_t byte = (uint8_t)fgetc(in);
        z |= ((UINT_T)(byte & 0x7F)) << shift;
        if ((byte & 0x80) == 0) break;
    }
    *(INT_T *)outval = (INT_T)((z >> 1L) ^ -(z & 1L)); // Zigzag decode
#endif
}

#ifdef __TINYC__
#define __builtin_add_overflow(x, y, result)                                                                           \
    ({                                                                                                                 \
        *(result) = (x) + (y);                                                                                         \
        false;                                                                                                         \
    })
#endif

public
Text_t NAMESPACED(as_text)(const void *i, bool colorize, const TypeInfo_t *info) {
    (void)info;
    if (!i) return Text(NAME_STR);
    Text_t text = _int64_to_text((int64_t)(*(INT_T *)i));
    return colorize ? Texts(Text("\033[35m"), text, Text("\033[m")) : text;
}
public
Text_t NAMESPACED(value_as_text)(INT_T i) { return _int64_to_text((int64_t)i); }
public
PUREFUNC int32_t NAMESPACED(compare)(const void *x, const void *y, const TypeInfo_t *info) {
    (void)info;
    return (*(INT_T *)x > *(INT_T *)y) - (*(INT_T *)x < *(INT_T *)y);
}
public
PUREFUNC bool NAMESPACED(equal)(const void *x, const void *y, const TypeInfo_t *info) {
    (void)info;
    return *(INT_T *)x == *(INT_T *)y;
}
public
CONSTFUNC bool NAMESPACED(is_between)(const INT_T x, const INT_T low, const INT_T high) {
    return low <= x && x <= high;
}
public
CONSTFUNC INT_T NAMESPACED(clamped)(INT_T x, INT_T min, INT_T max) { return x < min ? min : (x > max ? max : x); }
public
Text_t NAMESPACED(hex)(INT_T i, Int_t digits_int, bool uppercase, bool prefix) {
    Int_t as_int = Int$from_int64((int64_t)i);
    return Int$hex(as_int, digits_int, uppercase, prefix);
}
public
Text_t NAMESPACED(octal)(INT_T i, Int_t digits_int, bool prefix) {
    Int_t as_int = Int$from_int64((int64_t)i);
    return Int$octal(as_int, digits_int, prefix);
}
public
List_t NAMESPACED(bits)(INT_T x) {
    List_t bit_list = (List_t){.data = GC_MALLOC_ATOMIC(sizeof(bool[8 * sizeof(INT_T)])),
                               .atomic = 1,
                               .stride = sizeof(bool),
                               .length = 8 * sizeof(INT_T)};
    bool *bits = bit_list.data + sizeof(INT_T) * 8;
    for (size_t i = 0; i < 8 * sizeof(INT_T); i++) {
        *(bits--) = x & 1;
        x >>= 1;
    }
    return bit_list;
}
public
bool NAMESPACED(get_bit)(INT_T x, Int_t bit_index) {
    if (Int$compare_value(bit_index, I(1)) < 0) fail("Invalid bit index (expected 1 or higher): ", bit_index);
    if (Int$compare_value(bit_index, Int$from_int64(sizeof(INT_T) * 8)) > 0)
        fail("Bit index is too large! There are only ", (uint64_t)sizeof(INT_T) * 8,
             " bits, but index is: ", bit_index);
    return ((x & (INT_T)(1L << (Int64$from_int(bit_index, true) - 1L))) != 0);
}
typedef struct {
    OPT_T current, last;
    INT_T step;
} NAMESPACED(Range_t);

static OPT_T _next_int(NAMESPACED(Range_t) * info) {
    OPT_T i = info->current;
    if (i.has_value) {
        INT_T next;
        bool overflow = __builtin_add_overflow(i.value, info->step, &next);
        if (overflow || (info->last.has_value && (info->step >= 0 ? next > info->last.value : next < info->last.value)))
            info->current = (OPT_T){.has_value = false};
        else info->current = (OPT_T){.has_value = true, .value = next};
    }
    return i;
}

public
#if INTX_C_H__INT_BITS < 64
CONSTFUNC
#endif
Closure_t NAMESPACED(to)(INT_T first, INT_T last, OPT_T step) {
    NAMESPACED(Range_t) *range = GC_MALLOC(sizeof(NAMESPACED(Range_t)));
    range->current = (OPT_T){.has_value = true, .value = first};
    range->last = (OPT_T){.has_value = true, .value = last};
    range->step = step.has_value ? step.value : (last >= first ? 1 : -1);
    return (Closure_t){.fn = _next_int, .userdata = range};
}

public
#if INTX_C_H__INT_BITS < 64
CONSTFUNC
#endif
Closure_t NAMESPACED(onward)(INT_T first, INT_T step) {
    NAMESPACED(Range_t) *range = GC_MALLOC(sizeof(NAMESPACED(Range_t)));
    range->current = (OPT_T){.has_value = true, .value = first};
    range->last = (OPT_T){.has_value = false};
    range->step = step;
    return (Closure_t){.fn = _next_int, .userdata = range};
}
public
PUREFUNC OPT_T NAMESPACED(parse)(Text_t text, OptionalInt_t base, Text_t *remainder) {
    OptionalInt_t full_int = Int$parse(text, base, remainder);
    if (full_int.small == 0L) return (OPT_T){.has_value = false};
    if (Int$compare_value(full_int, I(NAMESPACED(min))) < 0) {
        return (OPT_T){.has_value = false};
    }
    if (Int$compare_value(full_int, I(NAMESPACED(max))) > 0) {
        return (OPT_T){.has_value = false};
    }
    return (OPT_T){.has_value = true, .value = NAMESPACED(from_int)(full_int, true)};
}

public
CONSTFUNC INT_T NAMESPACED(gcd)(INT_T x, INT_T y) {
    if (x == 0 || y == 0) return 0;
    x = NAMESPACED(abs)(x);
    y = NAMESPACED(abs)(y);
    while (x != y) {
        if (x > y) x -= y;
        else y -= x;
    }
    return x;
}

public
const INT_T NAMESPACED(min) =
#if INTX_C_H__INT_BITS == 64
    INT64_MIN
#elif INTX_C_H__INT_BITS == 32
    INT32_MIN
#elif INTX_C_H__INT_BITS == 16
    INT16_MIN
#elif INTX_C_H__INT_BITS == 8
    INT8_MIN
#else
#error "Unsupported integer bit width"
#endif
    ;

public
const INT_T NAMESPACED(max) =
#if INTX_C_H__INT_BITS == 64
    INT64_MAX
#elif INTX_C_H__INT_BITS == 32
    INT32_MAX
#elif INTX_C_H__INT_BITS == 16
    INT16_MAX
#elif INTX_C_H__INT_BITS == 8
    INT8_MAX
#else
#error "Unsupported integer bit width"
#endif
    ;

public
const TypeInfo_t NAMESPACED(info) = {
    .size = sizeof(INT_T),
    .align = __alignof__(INT_T),
    .metamethods =
        {
            .compare = NAMESPACED(compare),
            .as_text = NAMESPACED(as_text),
            .serialize = NAMESPACED(serialize),
            .deserialize = NAMESPACED(deserialize),
        },
};

#undef CAT_
#undef CAT
#undef STRINGIFY_
#undef STRINGIFY
#undef INT_T
#undef UINT_T
#undef OPT_T
#undef NAME_STR
#undef NAMESPACED
#undef INTX_C_H__INT_BITS
