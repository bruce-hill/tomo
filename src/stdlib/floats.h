// Type infos and methods for floats (floating point)

#pragma once

#define N64(n) ((double)(n))
typedef double OptionalFloat64_t;
#define FLOATX_H__BITS 64
#include "floatX.h"

#define N32(n) ((float)(n))
typedef float OptionalFloat32_t;
#define FLOATX_H__BITS 32
#include "floatX.h"
