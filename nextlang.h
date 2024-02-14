#pragma once

#include <err.h>
#include <gc.h>
#include <gc/cord.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "builtins/datatypes.h"

#define Int64_t int64_t
#define Int32_t int32_t
#define Int16_t int16_t
#define Int8_t int8_t
#define Int_t int64_t
#define I64(x) ((int64_t)x)
#define I32(x) ((int32_t)x)
#define I16(x) ((int16_t)x)
#define I8(x) ((int8_t)x)

#define Num64_t double
#define Num32_t float
#define Num_t double

#define String_t CORD
#define Str_t CORD

#define Bool_t bool
#define yes (Bool_t)true
#define no (Bool_t)false

#define Void_t void

#define __Array(t) array_t

CORD as_cord(void *x, bool use_color, const char *fmt, ...);

#define CORD_asprintf(...) ({ CORD __c; CORD_sprintf(&__c, __VA_ARGS__); __c; })
#define __declare(var, val) __typeof(val) var = val
#define __cord(x) _Generic(x, bool: x ? "yes" : "no", \
                         int8_t: CORD_asprintf("%d", x), \
                         int16_t: CORD_asprintf("%d", x), \
                         int32_t: CORD_asprintf("%d", x), int64_t: CORD_asprintf("%ld", x), \
                         double: CORD_asprintf("%g", x), float: CORD_asprintf("%g", x), \
                         CORD: x, \
                         char*: (CORD)({ const char *__str = x; __str && __str[0] ? __str : CORD_EMPTY;}), \
                         array_t: as_cord(&(x), false, "[ ]"), \
                         default: "???")
#define __heap(x) (__typeof(x)*)memcpy(GC_MALLOC(sizeof(x)), (__typeof(x)[1]){x}, sizeof(x))
#define __stack(x) (__typeof(x)*)((__typeof(x)[1]){x})
#define __length(x) _Generic(x, default: (x).length)
// Convert negative indices to back-indexed without branching: index0 = index + (index < 0)*(len+1)) - 1
#define __index(x, i) _Generic(x, array_t: ({ __typeof(x) __obj; int64_t __offset = i; __offset += (__offset < 0) * (__obj.length + 1) - 1; assert(__offset >= 0 && offset < __obj.length); __obj.data + __obj.stride * __offset;}))
#define __safe_index(x, i) _Generic(x, array_t: ({ __typeof(x) __obj; int64_t __offset = i - 1; __obj.data + __obj.stride * __offset;}))
#define __array(x, ...) ({ __typeof(x) __items[] = {x, __VA_ARGS__}; \
                         (__Array(__typeof(x))){.length=sizeof(__items)/sizeof(__items[0]), \
                         .stride=(int64_t)&__items[1] - (int64_t)&__items[0], \
                         .data=memcpy(GC_MALLOC(sizeof(__items)), __items,  sizeof(__items)), \
                         .copy_on_write=1}; })

#define not(x) _Generic(x, bool: (bool)!(x), default: ~(x))
#define and(x, y) _Generic(x, bool: (bool)((x) && (y)), default: ((x) & (y)))
#define or(x, y) _Generic(x, bool: (bool)((x) || (y)), default: ((x) | (y)))
#define xor(x, y) _Generic(x, bool: (bool)((x) ^ (y)), default: ((x) ^ (y)))
#define mod(x, n) ((x) % (n))
#define mod1(x, n) (((x) % (n)) + (__typeof(x))1)
#define __cmp(x, y) (_Generic(x, CORD: CORD_cmp(x, y), char*: strcmp(x, y), const char*: strcmp(x, y), default: (x > 0) - (y > 0)))
#define __lt(x, y) (bool)(_Generic(x, int8_t: x < y, int16_t: x < y, int32_t: x < y, int64_t: x < y, float: x < y, double: x < y, bool: x < y, \
                             default: __cmp(x, y) < 0))
#define __le(x, y) (bool)(_Generic(x, int8_t: x <= y, int16_t: x <= y, int32_t: x <= y, int64_t: x <= y, float: x <= y, double: x <= y, bool: x <= y, \
                             default: __cmp(x, y) <= 0))
#define __ge(x, y) (bool)(_Generic(x, int8_t: x >= y, int16_t: x >= y, int32_t: x >= y, int64_t: x >= y, float: x >= y, double: x >= y, bool: x >= y, \
                             default: __cmp(x, y) >= 0))
#define __gt(x, y) (bool)(_Generic(x, int8_t: x > y, int16_t: x > y, int32_t: x > y, int64_t: x > y, float: x > y, double: x > y, bool: x > y, \
                             default: __cmp(x, y) > 0))
#define __eq(x, y) (bool)(_Generic(x, int8_t: x == y, int16_t: x == y, int32_t: x == y, int64_t: x == y, float: x == y, double: x == y, bool: x == y, \
                             default: __cmp(x, y) == 0))
#define __ne(x, y) (bool)(_Generic(x, int8_t: x != y, int16_t: x != y, int32_t: x != y, int64_t: x != y, float: x != y, double: x != y, bool: x != y, \
                             default: __cmp(x, y) != 0))
#define min(x, y) ({ __declare(__min_lhs, x); __declare(__min_rhs, y); __le(__min_lhs, __min_rhs) ? __min_lhs : __min_rhs; })
#define max(x, y) ({ __declare(__min_lhs, x); __declare(__min_rhs, y); __ge(__min_lhs, __min_rhs) ? __min_lhs : __min_rhs; })

#define say(str) puts(CORD_to_const_char_star(__cord(str)))
#define __test(src, expr, expected) do { \
        CORD __result = __cord(expr); \
        say(CORD_catn(5, USE_COLOR ? "\x1b[33;1m>>\x1b[0m " : ">> ", src, USE_COLOR ? "\n\x1b[0;2m=\x1b[m " : "\n= ", __result, "\x1b[m")); \
        if (expected && CORD_cmp(__result, expected)) { \
            fprintf(stderr, USE_COLOR ? "\x1b[31;1;7mTEST FAILURE!\x1b[27m\nI expected:\n\t\x1b[0;1m%s\x1b[1;31m\nbut got:\n\t%s\x1b[m\n" : "TEST FAILURE!\nI expected:\n\t%s\nbut got:\n\t%s\n", CORD_to_const_char_star(expected), CORD_to_const_char_star(__result)); \
            raise(SIGABRT); \
        } \
    } while (0)

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
