#pragma once

#include <gc.h>
#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "builtins/datatypes.h"

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

#define __Array(t) array_t

#define CORD_asprintf(...) ({ CORD __c; CORD_sprintf(&__c, __VA_ARGS__); __c; })
#define __declare(var, val) __typeof(val) var = val
#define __cord(x) _Generic(x, bool: x ? "yes" : "no", \
                         int8_t: CORD_asprintf("%d", x), \
                         int16_t: CORD_asprintf("%d", x), \
                         int32_t: CORD_asprintf("%d", x), int64_t: CORD_asprintf("%ld", x), \
                         double: CORD_asprintf("%g", x), float: CORD_asprintf("%g", x), \
                         CORD: x, \
                         char*: x, \
                         char: CORD_cat_char(NULL, x), \
                         default: "???")
#define __heap(x) (__typeof(x)*)memcpy(GC_MALLOC(sizeof(x)), (__typeof(x)[1]){x}, sizeof(x))
#define __stack(x) (&(__typeof(x)){x})
#define __length(x) _Generic(x, default: (x).length)
// Convert negative indices to back-indexed without branching: index0 = index + (index < 0)*(len+1)) - 1
#define __index(x, i) _Generic(x, array_t: ({ __typeof(x) __obj; int64_t __offset = i; __offset += (__offset < 0) * (__obj.length + 1) - 1; assert(__offset >= 0 && offset < __obj.length); __obj.data + __obj.stride * __offset;}))
#define __safe_index(x, i) _Generic(x, array_t: ({ __typeof(x) __obj; int64_t __offset = i - 1; __obj.data + __obj.stride * __offset;}))
#define __array(x, ...) ({ __typeof(x) __items[] = {x, __VA_ARGS__}; \
                         (__Array(__typeof(x))){.length=sizeof(__items)/sizeof(__items[0]), \
                         .stride=(int64_t)&__items[1] - (int64_t)&__items[0], \
                         .data=memcpy(GC_MALLOC(sizeof(__items)), __items,  sizeof(__items)), \
                         .copy_on_write=1}; })

#define not(x) _Generic(x, bool: !(x), default: ~(x))
#define and(x, y) _Generic(x, bool: (x) && (y), default: (x) & (y))
#define or(x, y) _Generic(x, bool: (x) || (y), default: (x) | (y))
#define xor(x, y) ((x) ^ (y))
#define mod(x, n) ((x) % (n))
#define mod1(x, n) (((x) % (n)) + (__typeof(x))1)

#define say(str) puts(CORD_to_const_char_star(str))

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
