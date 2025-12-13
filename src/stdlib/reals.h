#include <gmp.h>
#include <stdint.h>

#include "datatypes.h"
#include "types.h"

#define NONE_REAL ((Real_t)NULL)

Int_t Real$compute(Real_t r, int64_t decimals);
Text_t Real$value_as_text(Real_t x, int64_t decimals);

OptionalReal_t Real$parse(Text_t text, Text_t *remainder);
Real_t Real$from_text(Text_t text);
Real_t Real$from_float64(double n);
Real_t Real$from_int(Int_t i);

double Real$as_float64(Real_t x);
Int_t Real$as_int(Real_t x);

Real_t Real$negative(Real_t x);
// Real_t Real$inverse(Real_t x);
Real_t Real$plus(Real_t x, Real_t y);
Real_t Real$minus(Real_t x, Real_t y);
Real_t Real$times(Real_t x, Real_t y);
Real_t Real$divided_by(Real_t x, Real_t y);
Real_t Real$left_shifted(Real_t x, Int_t amount);
Real_t Real$right_shifted(Real_t x, Int_t amount);

extern const TypeInfo_t Real$info;
