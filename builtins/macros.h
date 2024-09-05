#pragma once

// A few helper macros

#include <gc.h>
#include <string.h>

#define heap(x) (__typeof(x)*)memcpy(GC_MALLOC(sizeof(x)), (__typeof(x)[1]){x}, sizeof(x))
#define stack(x) (__typeof(x)*)((__typeof(x)[1]){x})
