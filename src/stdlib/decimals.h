#pragma once

// Integer type infos and methods

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "print.h"
#include "datatypes.h"
#include "stdlib.h"
#include "types.h"
#include "util.h"

#define NONE_DEC (((union { int64_t i; _Decimal64 d; }){.i=-1}).d)

int Dec$print(FILE *f, Dec_t d);
Text_t Dec$value_as_text(Dec_t d);
Text_t Dec$as_text(const void *d, bool colorize, const TypeInfo_t *info);
CONSTFUNC int32_t Dec$compare_value(const Dec_t x, const Dec_t y);
CONSTFUNC int32_t Dec$compare(const void *x, const void *y, const TypeInfo_t *info);
CONSTFUNC bool Dec$equal_value(const Dec_t x, const Dec_t y);
CONSTFUNC bool Dec$equal(const void *x, const void *y, const TypeInfo_t *info);
CONSTFUNC Dec_t Dec$round(Dec_t d, Int_t digits);
CONSTFUNC Dec_t Dec$power(Dec_t base, Dec_t exponent);
CONSTFUNC Dec_t Dec$plus(Dec_t x, Dec_t y);
CONSTFUNC Dec_t Dec$negative(Dec_t x);
CONSTFUNC Dec_t Dec$minus(Dec_t x, Dec_t y);
CONSTFUNC Dec_t Dec$times(Dec_t x, Dec_t y);
CONSTFUNC Dec_t Dec$divided_by(Dec_t x, Dec_t y);
CONSTFUNC Dec_t Dec$modulo(Dec_t x, Dec_t modulus);
CONSTFUNC Dec_t Dec$modulo1(Dec_t x, Dec_t modulus);
PUREFUNC Dec_t Dec$from_str(const char *str);
OptionalDec_t Dec$parse(Text_t text);

CONSTFUNC Dec_t Dec$from_int64(int64_t i);
Dec_t Dec$from_int(Int_t i);
CONSTFUNC Dec_t Dec$from_num(double n);
#define Dec$from_num32(n) Dec$from_num((double)n)
#define Dec$from_int32(i) Dec$from_int64((int64_t)i)
#define Dec$from_int16(i) Dec$from_int64((int64_t)i)
#define Dec$from_int8(i) Dec$from_int64((int64_t)i)
#define Dec$from_byte(i) Dec$from_int64((int64_t)i)
#define Dec$from_bool(i) Dec$from_int64((int64_t)i)

Int_t Dec$as_int(Dec_t d, bool truncate);
int64_t Dec$as_int64(Dec_t d, bool truncate);
int32_t Dec$as_int32(Dec_t d, bool truncate);
int16_t Dec$as_int16(Dec_t d, bool truncate);
int8_t Dec$as_int8(Dec_t d, bool truncate);
Byte_t Dec$as_byte(Dec_t d, bool truncate);
CONSTFUNC bool Dec$as_bool(Dec_t d);
double Dec$as_num(Dec_t d);
#define Dec$as_num32(d) ((float)Dec$as_num(d))

extern const TypeInfo_t Dec$info;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
