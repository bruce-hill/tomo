// Integer type infos and methods
#include <stdio.h> // Must be before gmp.h

#include <ctype.h>
#include <gc.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "bytes.h"
#include "datatypes.h"
#include "decimals.h"
#include "integers.h"
#include "lists.h"
#include "nums.h"
#include "optionals.h"
#include "print.h"
#include "text.h"
#include "types.h"

public int Dec$print(FILE *f, Dec_t d) {
    return fprint(f, d);
}

public Text_t Dec$value_as_text(Dec_t d) {
    return Text$from_str(String(d));
}

public Text_t Dec$as_text(const void *d, bool colorize, const TypeInfo_t *info) {
    (void)info;
    if (!d) return Text("Dec");
    Text_t text = Text$from_str(String(*(Dec_t*)d));
    if (colorize) text = Text$concat(Text("\x1b[35m"), text, Text("\x1b[m"));
    return text;
}

static bool Dec$is_none(const void *d, const TypeInfo_t *info)
{
    (void)info;
    return *(int64_t*)d == -1;
}

public CONSTFUNC int32_t Dec$compare_value(const Dec_t x, const Dec_t y) {
    return (x > y) - (x < y);
}

public CONSTFUNC int32_t Dec$compare(const void *x, const void *y, const TypeInfo_t *info) {
    (void)info;
    return Dec$compare_value(*(Dec_t*)x, *(Dec_t*)y);
}

public CONSTFUNC bool Dec$equal_value(const Dec_t x, const Dec_t y) {
    return x == y;
}

public CONSTFUNC bool Dec$equal(const void *x, const void *y, const TypeInfo_t *info) {
    (void)info;
    return *(_Decimal64*)x == *(_Decimal64*)y;
}

public CONSTFUNC Dec_t Dec$plus(Dec_t x, Dec_t y) {
    return x + y;
}

public CONSTFUNC Dec_t Dec$negative(Dec_t x) {
    return -x;
}

public CONSTFUNC Dec_t Dec$minus(Dec_t x, Dec_t y) {
    return x - y;
}

public CONSTFUNC Dec_t Dec$times(Dec_t x, Dec_t y) {
    return x * y;
}

public CONSTFUNC Dec_t Dec$divided_by(Dec_t x, Dec_t y) {
    return x / y;
}

public CONSTFUNC Dec_t Dec$modulo(Dec_t x, Dec_t modulus) {
    // TODO: improve the accuracy of this approach:
    return (Dec_t)Num$mod((double)x, (double)modulus);
}

public CONSTFUNC Dec_t Dec$modulo1(Dec_t x, Dec_t modulus) {
    // TODO: improve the accuracy of this approach:
    return (Dec_t)Num$mod1((double)x, (double)modulus);
}

public PUREFUNC OptionalDec_t Dec$from_str(const char *str) {
    _Decimal64 n = 0.0DD;
    const char *p = str;
    bool negative = (*p == '-');
    if (negative)
        p += 1;
    for (; *p; p++) {
        if (*p == '.') break;
        if (*p == '_') continue;
        if (!isdigit(*p)) return NONE_DEC;
        n = 10.0DD * n + (_Decimal64)(*p - '0');
    }
    _Decimal64 denominator = 1.0DD;
    for (; *p; p++) {
        if (*p == '_') continue;
        if (!isdigit(*p)) return NONE_DEC;
        n = 10.0DD * n + (_Decimal64)(*p - '0');
        denominator *= 0.1DD;
    }
    return n * denominator;
}

public CONSTFUNC Dec_t Dec$from_int64(int64_t i) {
    return (_Decimal64)i;
}

public Dec_t Dec$from_int(Int_t i) {
    if likely (i.small & 1L) {
        return Dec$from_int64(i.small >> 2L);
    }
    Text_t text = Int$value_as_text(i);
    const char *str = Text$as_c_string(text);
    return Dec$from_str(str);
}

CONSTFUNC public Dec_t Dec$from_num(double n) {
    return (_Decimal64)n;
}

public Int_t Dec$as_int(Dec_t d, bool truncate) {
    char *str = String(d);
    char *decimal = strchr(str, '.');
    if (!truncate && decimal)
        fail("Could not convert to an integer without truncation: ", str);
    *decimal = '\0';
    return Int$from_str(str);
}

public int64_t Dec$as_int64(Dec_t d, bool truncate) {
    return Int64$from_int(Dec$as_int(d, truncate), truncate);
}

public int32_t Dec$as_int32(Dec_t d, bool truncate) {
    return Int32$from_int(Dec$as_int(d, truncate), truncate);
}

public int16_t Dec$as_int16(Dec_t d, bool truncate) {
    return Int16$from_int(Dec$as_int(d, truncate), truncate);
}

public int8_t Dec$as_int8(Dec_t d, bool truncate) {
    return Int8$from_int(Dec$as_int(d, truncate), truncate);
}

public Byte_t Dec$as_byte(Dec_t d, bool truncate) {
    return Byte$from_int(Dec$as_int(d, truncate), truncate);
}

CONSTFUNC public bool Dec$as_bool(Dec_t d) {
    return d != 0.0DD;
}

public double Dec$as_num(Dec_t d) {
    const char *str = String(d);
    return strtod(str, NULL);
}

#define NAN_MASK 0x7C00000000000000UL
#define INF_MASK 0x7800000000000000UL
#define DEC_BITS(n) ((union { uint64_t bits; _Decimal64 d; }){.d=n}).bits

static bool Dec$isfinite(Dec_t d) {
    uint64_t bits = DEC_BITS(d);
    return (((bits & NAN_MASK) != NAN_MASK) &&
            ((bits & INF_MASK) != INF_MASK));
}

static bool Dec$isnan(Dec_t d) {
    uint64_t bits = DEC_BITS(d);
    return ((bits & NAN_MASK) == NAN_MASK);
}

CONSTFUNC static Dec_t Dec$int_power(Dec_t x, int64_t exponent)
{
    if (exponent == 0) {
        return 1.DD;
    } else if (exponent == 1) {
        return x;
    } else if (exponent % 2 == 0) {
        Dec_t y = Dec$int_power(x, exponent/2);
        return y*y;
    } else {
        return x * Dec$int_power(x, exponent - 1);
    }
}

public Dec_t Dec$power(Dec_t x, Dec_t y) {
    if (x == 0.DD && y < 0.DD)
        fail("The following math operation is not supported: ", x, "^", y);

    /* For any y, including a NaN. */
    if (x == 1.DD)
        return x;

    if (Dec$isnan(x) || Dec$isnan(y))
        return NONE_DEC;

    if (y == 0.DD)
        return 1.DD;

    if (x < 0.DD && y < 0.DD) {
        return NONE_DEC;
    } else if (x == 0.DD) {
        return y < 0.DD ? NONE_DEC : 0.DD;
    } else if (!Dec$isfinite(x)) {
        return y < 0.DD ? 0.DD : x;
    }

    int64_t int_y = (int64_t)y;
    if ((Dec_t)int_y == y)
        return Dec$int_power(x, int_y);

    // TODO: improve the accuracy of this approach:
    return (Dec_t)powl((long double)x, (long double)y);
}

public Dec_t Dec$round(Dec_t d, Int_t digits) {
    int64_t digits64 = Int64$from_int(digits, false);
    if (digits.small != 1L) {
        for (int64_t i = digits64; i > 0; i--)
            d *= 10.0DD;
        for (int64_t i = digits64; i < 0; i++)
            d *= 0.1DD;
    }
    _Decimal64 truncated = (_Decimal64)(int64_t)d;
    _Decimal64 difference = (d - truncated);
    _Decimal64 rounded;
    if (difference < 0.0DD) {
        rounded = (difference < -0.5DD) ? truncated - 1.0DD : truncated;
    } else {
        rounded = (difference >= 0.5DD) ? truncated + 1.0DD : truncated;
    }
    for (int64_t i = digits64; i > 0; i--)
        rounded *= 0.1DD;
    for (int64_t i = digits64; i < 0; i++)
        rounded *= 10.0DD;
    return rounded;
}

public OptionalDec_t Dec$parse(Text_t text) {
    return Dec$from_str(Text$as_c_string(text));
}

static void Dec$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *info)
{
    (void)info;
    Dec_t d = *(Dec_t*)obj;
    char *str = String(d);
    int64_t len = (int64_t)strlen(str);
    Int64$serialize(&len, out, pointers, &Int64$info);
    if (fwrite(str, sizeof(char), (size_t)len, out) != (size_t)len)
        fail("Could not serialize Dec value!");
}

static void Dec$deserialize(FILE *in, void *obj, List_t *pointers, const TypeInfo_t *info)
{
    (void)info;
    int64_t len = 0;
    Int64$deserialize(in, &len, pointers, &Int64$info);
    assert(len >= 0);
    char buf[len];
    if (fread(buf, sizeof(char), (size_t)len, in) != (size_t)len)
        fail("Could not deserialize Dec value!");
    Dec_t d = Dec$from_str(buf);
    memcpy(obj, &d, sizeof(d));
}

public const TypeInfo_t Dec$info = {
    .size=sizeof(Dec_t),
    .align=__alignof__(Dec_t),
    .metamethods={
        .compare=Dec$compare,
        .equal=Dec$equal,
        .as_text=Dec$as_text,
        .is_none=Dec$is_none,
        .serialize=Dec$serialize,
        .deserialize=Dec$deserialize,
    },
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
