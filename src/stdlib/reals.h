#include <gmp.h>
#include <stdint.h>

#include "datatypes.h"

Int_t Real$compute(Real_t r, int64_t precision);
Text_t Real$value_as_text(Real_t x, int64_t digits);

Real_t Real$from_float64(double n);
Real_t Real$from_int(Int_t i);

double Real$as_float64(Real_t x);
Int_t Real$as_int(Real_t x);

Real_t Real$negative(Real_t x);
Real_t Real$plus(Real_t x, Real_t y);
Real_t Real$minus(Real_t x, Real_t y);
Real_t Real$times(Real_t x, Real_t y);
Real_t Real$divided_by(Real_t x, Real_t y);
Real_t Real$left_shifted(Real_t x, Int_t amount);
Real_t Real$right_shifted(Real_t x, Int_t amount);
