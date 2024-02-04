#pragma once

#include <gc.h>
#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define Int64_t int64_t
#define Int32_t int32_t
#define Int16_t int16_t
#define Int8_t int8_t

#define Num64_t double
#define Num32_t float

#define String_t CORD

#define Char_t char

#define Bool_t bool

#define Void_t void

#ifndef auto
#define auto __auto_type
#endif

#define CORD_asprintf(...) ({ CORD __c; CORD_sprintf(&__c, __VA_ARGS__); __c; })
#define __declare(var, val) __typeof(val) var = val
#define __cord(x) _Generic(x, bool: x ? "yes" : "no", \
                         int16_t: CORD_asprintf("%d", x), int32_t: CORD_asprintf("%d", x), int64_t: CORD_asprintf("%ld", x), \
                         double: CORD_asprintf("%g", x), float: CORD_asprintf("%g", x), \
                         CORD: x, \
                         default: "???")
#define __heap(x) (__typeof(x)*)memcpy(GC_MALLOC(sizeof(x)), (__typeof(x)[1]){x}, sizeof(x))
#define __stack(x) (&(__typeof(x)){x})

#define say(str) puts(CORD_to_const_char_star(str))
