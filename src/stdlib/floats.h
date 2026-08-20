// Type infos and methods for floats (floating point)

#pragma once

#define N64(n) ((double)(n))
#define OptionalFloat64_t double
#define FLOATX_H__BITS 64
#include "floatX.h"

#define N32(n) ((float)(n))
#define OptionalFloat32_t float
#define FLOATX_H__BITS 32
#include "floatX.h"
