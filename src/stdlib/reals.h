#include <gmp.h>
#include <stdint.h>

#include "datatypes.h"
#include "types.h"

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

Text_t Real$value_as_text(Real_t x);

OptionalReal_t Real$parse(Text_t text, Text_t *remainder);
Real_t Real$from_text(Text_t text);
Real_t Real$from_float64(double n);
Real_t Real$from_int(Int_t i);

double Real$as_float64(Real_t n, bool truncate);
Int_t Real$as_int(Real_t x, bool truncate);

Real_t Real$negative(Real_t x);
Real_t Real$plus(Real_t x, Real_t y);
Real_t Real$minus(Real_t x, Real_t y);
Real_t Real$times(Real_t x, Real_t y);
Real_t Real$divided_by(Real_t x, Real_t y);
Real_t Real$power(Real_t base, Real_t exp);
Real_t Real$sqrt(Real_t x);
bool Real$equal(const void *va, const void *vb, const TypeInfo_t *t);
int32_t Real$compare(const void *va, const void *vb, const TypeInfo_t *t);

Real_t Real$rounded_to(Real_t x, Real_t round_to);

int Real$test();

extern const TypeInfo_t Real$info;
