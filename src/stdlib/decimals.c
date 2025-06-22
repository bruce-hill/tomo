// Integer type infos and methods
#include <stdio.h> // Must be before gmp.h

#include <ctype.h>
#include <gc.h>
#include <mpdecimal.h>
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
#include "siphash.h"
#include "text.h"
#include "types.h"

static mpd_context_t ctx = {
    .prec=30,
    .emax=25,
    .emin=-25,
    .traps=MPD_Division_by_zero | MPD_Overflow | MPD_Subnormal | MPD_Underflow,
    .status=0,
    .newtrap=0,
    .round=MPD_ROUND_HALF_EVEN,
    .clamp=1,
    .allcr=1,
};

static inline char *Dec$as_str(Dec_t d) {
    char *str = mpd_format(d, "f", &ctx);
    char *point = strchr(str, '.');
    if (point == NULL) return str;
    char *p;
    for (p = point + strlen(point)-1; p > point && *p == '0'; p--)
        *p = '\0';
    if (*p == '.') *p = '\0';
    return str;
}

public int Dec$print(FILE *f, Dec_t d) {
    return fputs(Dec$as_str(d), f);
}

public Text_t Dec$value_as_text(Dec_t d) {
    return Text$from_str(Dec$as_str(d));
}

public Text_t Dec$as_text(const void *d, bool colorize, const TypeInfo_t *info) {
    (void)info;
    if (!d) return Text("Dec");
    Text_t text = Text$from_str(Dec$as_str(*(Dec_t*)d));
    if (colorize) text = Text$concat(Text("\x1b[35m"), text, Text("\x1b[m"));
    return text;
}

static bool Dec$is_none(const void *d, const TypeInfo_t *info)
{
    (void)info;
    return *(Dec_t*)d == NULL;
}

public PUREFUNC int32_t Dec$compare_value(const Dec_t x, const Dec_t y) {
    return mpd_compare(D(0), x, y, &ctx);
}

public PUREFUNC int32_t Dec$compare(const void *x, const void *y, const TypeInfo_t *info) {
    (void)info;
    return mpd_compare(D(0), *(Dec_t*)x, *(Dec_t*)y, &ctx);
}

public PUREFUNC bool Dec$equal_value(const Dec_t x, const Dec_t y) {
    return Dec$compare_value(x, y) == 0;
}

public PUREFUNC bool Dec$equal(const void *x, const void *y, const TypeInfo_t *info) {
    (void)info;
    return Dec$compare(x, y, info) == 0;
}

public PUREFUNC uint64_t Dec$hash(const void *vx, const TypeInfo_t *info) {
    (void)info;
    Dec_t d = *(Dec_t*)vx;
    char *str = Dec$as_str(d);
    return siphash24((void*)str, strlen(str));
}

public Dec_t Dec$plus(Dec_t x, Dec_t y) {
    Dec_t result = mpd_new(&ctx);
    mpd_add(result, x, y, &ctx);
    return result;
}

public Dec_t Dec$negative(Dec_t x) {
    Dec_t result = mpd_new(&ctx);
    mpd_minus(result, x, &ctx);
    return result;
}

public Dec_t Dec$minus(Dec_t x, Dec_t y) {
    Dec_t result = mpd_new(&ctx);
    mpd_sub(result, x, y, &ctx);
    return result;
}

public Dec_t Dec$times(Dec_t x, Dec_t y) {
    Dec_t result = mpd_new(&ctx);
    mpd_mul(result, x, y, &ctx);
    return result;
}

public Dec_t Dec$divided_by(Dec_t x, Dec_t y) {
    Dec_t result = mpd_new(&ctx);
    mpd_div(result, x, y, &ctx);
    return result;
}

public Dec_t Dec$modulo(Dec_t x, Dec_t modulus) {
    Dec_t result = mpd_new(&ctx);
    mpd_rem(result, x, modulus, &ctx);
    return result;
}

public Dec_t Dec$modulo1(Dec_t x, Dec_t modulus) {
    return Dec$plus(Dec$modulo(Dec$minus(x, D(1)), modulus), D(1));
}

public Dec_t Dec$from_str(const char *str) {
    Dec_t result = mpd_new(&ctx);
    mpd_set_string(result, str, &ctx);
    return result;
}

public Dec_t Dec$from_int64(int64_t i) {
    Dec_t result = mpd_new(&ctx);
    mpd_set_i64(result, i, &ctx);
    return result;
}

public Dec_t Dec$from_int(Int_t i) {
    if likely (i.small & 1L) {
        return Dec$from_int64(i.small >> 2L);
    }
    Text_t text = Int$value_as_text(i);
    const char *str = Text$as_c_string(text);
    Dec_t result = mpd_new(&ctx);
    mpd_set_string(result, str, &ctx);
    return result;
}

public Dec_t Dec$from_num(double n) {
    Text_t text = Num$as_text(&n, false, &Num$info);
    const char *str = Text$as_c_string(text);
    Dec_t result = mpd_new(&ctx);
    mpd_set_string(result, str, &ctx);
    return result;
}

public Int_t Dec$as_int(Dec_t d, bool truncate) {
    char *str = Dec$as_str(d);
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

public bool Dec$as_bool(Dec_t d) {
    return !mpd_iszero(d);
}

public double Dec$as_num(Dec_t d) {
    const char *str = Dec$as_str(d);
    return strtod(str, NULL);
}

public Dec_t Dec$power(Dec_t base, Dec_t exponent) {
    Dec_t result = mpd_new(&ctx);
    mpd_pow(result, base, exponent, &ctx);
    return result;
}

public Dec_t Dec$round(Dec_t d, Int_t digits) {
    Dec_t result = mpd_new(&ctx);
    if (digits.small != 1L)
        d = Dec$times(d, Dec$power(D(10), Dec$from_int(digits)));
    mpd_round_to_int(result, d, &ctx);
    if (digits.small != 1L)
        result = Dec$divided_by(result, Dec$power(D(10), Dec$from_int(digits)));
    return result;
}

public OptionalDec_t Dec$parse(Text_t text) {
    Dec_t result = mpd_new(&ctx);
    uint32_t status = 0;
    mpd_qset_string(result, Text$as_c_string(text), &ctx, &status);
    return status == 0 ? result : NONE_DEC;
}

static void Dec$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *info)
{
    (void)info;
    Dec_t d = *(Dec_t*)obj;
    char *str = Dec$as_str(d);
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
        .hash=Dec$hash,
        .as_text=Dec$as_text,
        .is_none=Dec$is_none,
        .serialize=Dec$serialize,
        .deserialize=Dec$deserialize,
    },
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
