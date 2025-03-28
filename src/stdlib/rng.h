#pragma once

// Random Number Generator (RNG) functions/type info

#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "types.h"
#include "bools.h"
#include "bytes.h"
#include "util.h"

RNG_t RNG$new(Array_t seed);
void RNG$set_seed(RNG_t rng, Array_t seed);
RNG_t RNG$copy(RNG_t rng);
Bool_t RNG$bool(RNG_t rng, Num_t p);
Int_t RNG$int(RNG_t rng, Int_t min, Int_t max);
Int64_t RNG$int64(RNG_t rng, Int64_t min, Int64_t max);
Int32_t RNG$int32(RNG_t rng, Int32_t min, Int32_t max);
Int16_t RNG$int16(RNG_t rng, Int16_t min, Int16_t max);
Int8_t RNG$int8(RNG_t rng, Int8_t min, Int8_t max);
Byte_t RNG$byte(RNG_t rng);
Array_t RNG$bytes(RNG_t rng, Int_t count);
Num_t RNG$num(RNG_t rng, Num_t min, Num_t max);
Num32_t RNG$num32(RNG_t rng, Num32_t min, Num32_t max);

extern const TypeInfo_t RNG$info;
// TinyCC doesn't implement _Thread_local
#ifdef __TINYC__
extern RNG_t default_rng;
#else
extern _Thread_local RNG_t default_rng;
#endif

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
