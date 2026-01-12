#include <gmp.h>
#include <stdint.h>

#include "datatypes.h"
#include "types.h"
#include "util.h"

// NaN-boxing scheme: use quiet NaN space for pointers
// IEEE 754: NaN = exponent all 1s, mantissa non-zero
// Quiet NaN: sign bit can be anything, bit 51 = 1
#define QNAN_MASK 0x7FF8000000000000ULL
#define TAG_MASK 0x0007000000000000ULL
#define PTR_MASK 0x0000FFFFFFFFFFFFULL

#define REAL_TAG_BIGINT 0x0001000000000000ULL
#define REAL_TAG_RATIONAL 0x0002000000000000ULL
#define REAL_TAG_CONSTRUCTIVE 0x0003000000000000ULL
#define REAL_TAG_SYMBOLIC 0x0004000000000000ULL
#define REAL_TAG_NONE 0x0005000000000000ULL

#define NONE_REAL ((Real_t){.u64 = QNAN_MASK | REAL_TAG_NONE})

CONSTFUNC Real_t Real$from_float64(double n);
CONSTFUNC bool Real$is_boxed(Real_t n);
CONSTFUNC uint64_t Real$tag(Real_t n);
Int_t Real$as_int(Real_t x, bool truncate);
OptionalReal_t Real$parse(Text_t text, Text_t *remainder);
PUREFUNC bool Real$is_none(const void *vn, const TypeInfo_t *type);
Real_t Real$abs(Real_t x);
Real_t Real$acos(Real_t x);
Real_t Real$asin(Real_t x);
Real_t Real$atan(Real_t x);
Real_t Real$atan2(Real_t y, Real_t x);
Real_t Real$ceil(Real_t x);
Real_t Real$clamped(Real_t x, Real_t low, Real_t high);
Real_t Real$cos(Real_t x);
Real_t Real$divided_by(Real_t x, Real_t y);
Real_t Real$exp(Real_t x);
Real_t Real$floor(Real_t x);
Real_t Real$from_int(Int_t i);
Real_t Real$from_rational(int64_t num, int64_t den);
Real_t Real$from_text(Text_t text);
Real_t Real$log(Real_t x);
Real_t Real$log10(Real_t x);
Real_t Real$minus(Real_t x, Real_t y);
Real_t Real$mix(Real_t amount, Real_t x, Real_t y);
Real_t Real$mod(Real_t n, Real_t modulus);
Real_t Real$mod1(Real_t n, Real_t modulus);
Real_t Real$negative(Real_t x);
Real_t Real$plus(Real_t x, Real_t y);
Real_t Real$power(Real_t base, Real_t exp);
Real_t Real$rounded_to(Real_t x, Real_t round_to);
Real_t Real$sin(Real_t x);
Real_t Real$sqrt(Real_t x);
Real_t Real$tan(Real_t x);
Real_t Real$times(Real_t x, Real_t y);
Text_t Real$as_text(const void *n, bool colorize, const TypeInfo_t *type);
Text_t Real$value_as_text(Real_t x);
bool Real$equal(const void *va, const void *vb, const TypeInfo_t *t);
bool Real$get_rational(Real_t x, int64_t *num, int64_t *den);
bool Real$is_between(Real_t x, Real_t low, Real_t high);
double Real$as_float64(Real_t n, bool truncate);
int32_t Real$compare(const void *va, const void *vb, const TypeInfo_t *t);

int Real$test();

extern const TypeInfo_t Real$info;
