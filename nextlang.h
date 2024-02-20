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

#include "builtins/array.h"
#include "builtins/bool.h"
#include "builtins/color.h"
#include "builtins/datatypes.h"
#include "builtins/functions.h"
#include "builtins/integers.h"
#include "builtins/memory.h"
#include "builtins/nums.h"
#include "builtins/pointer.h"
#include "builtins/string.h"
#include "builtins/table.h"
#include "builtins/types.h"

#define Void_t void

CORD as_cord(void *x, bool use_color, const char *fmt, ...);

#define StrF(...) ({ CORD $c; CORD_sprintf(&$c, __VA_ARGS__); $c; })
#define $var(var, val) __typeof(val) var = val
#define $cord(x) _Generic(x, bool: x ? "yes" : "no", \
                         int8_t: StrF("%d", x), \
                         int16_t: StrF("%d", x), \
                         int32_t: StrF("%d", x), int64_t: StrF("%ld", x), \
                         double: StrF("%g", x), float: StrF("%g", x), \
                         CORD: x, \
                         array_t: as_cord($stack(x), false, "[ ]"), \
                         default: "???")
#define $heap(x) (__typeof(x)*)memcpy(GC_MALLOC(sizeof(x)), (__typeof(x)[1]){x}, sizeof(x))
#define $stack(x) (__typeof(x)*)((__typeof(x)[1]){x})
#define $tagged(obj_expr, type_name, tag_name) ({ __typeof(obj_expr) $obj = obj_expr; \
                                                $obj.$tag == $tag$##type_name##$##tag_name ? &$obj.tag_name : NULL; })


#define not(x) _Generic(x, bool: (bool)!(x), int64_t: ~(x), int32_t: ~(x), int16_t: ~(x), int8_t: ~(x), \
                        array_t: ((x).length == 0), table_t: ((x).entries.length == 0), CORD: ((x) == CORD_EMPTY), \
                        default: _Static_assert(0, "Not supported"))
#define Bool(x) _Generic(x, bool: (bool)(x), int64_t: (x != 0), int32_t: (x != 0), int16_t: (x != 0), int8_t: (x != 0), CORD: ((x) == CORD_EMPTY), \
                         array_t: ((x).length > 0), table_t: ((x).entries.length > 0), CORD: ((x) != CORD_EMPTY), \
                         default: _Static_assert(0, "Not supported"))
#define and(x, y) _Generic(x, bool: (bool)((x) && (y)), default: ((x) & (y)))
#define or(x, y) _Generic(x, bool: (bool)((x) || (y)), default: ((x) | (y)))
#define xor(x, y) _Generic(x, bool: (bool)((x) ^ (y)), default: ((x) ^ (y)))
#define mod(x, n) ((x) % (n))
#define mod1(x, n) (((x) % (n)) + (__typeof(x))1)
#define $cmp(x, y) (_Generic(x, CORD: CORD_cmp(x, y), char*: strcmp(x, y), const char*: strcmp(x, y), default: (x > 0) - (y > 0)))
#define $lt(x, y) (bool)(_Generic(x, int8_t: x < y, int16_t: x < y, int32_t: x < y, int64_t: x < y, float: x < y, double: x < y, bool: x < y, \
                             default: $cmp(x, y) < 0))
#define $le(x, y) (bool)(_Generic(x, int8_t: x <= y, int16_t: x <= y, int32_t: x <= y, int64_t: x <= y, float: x <= y, double: x <= y, bool: x <= y, \
                             default: $cmp(x, y) <= 0))
#define $ge(x, y) (bool)(_Generic(x, int8_t: x >= y, int16_t: x >= y, int32_t: x >= y, int64_t: x >= y, float: x >= y, double: x >= y, bool: x >= y, \
                             default: $cmp(x, y) >= 0))
#define $gt(x, y) (bool)(_Generic(x, int8_t: x > y, int16_t: x > y, int32_t: x > y, int64_t: x > y, float: x > y, double: x > y, bool: x > y, \
                             default: $cmp(x, y) > 0))
#define $eq(x, y) (bool)(_Generic(x, int8_t: x == y, int16_t: x == y, int32_t: x == y, int64_t: x == y, float: x == y, double: x == y, bool: x == y, \
                             default: $cmp(x, y) == 0))
#define $ne(x, y) (bool)(_Generic(x, int8_t: x != y, int16_t: x != y, int32_t: x != y, int64_t: x != y, float: x != y, double: x != y, bool: x != y, \
                             default: $cmp(x, y) != 0))
#define min(x, y) ({ $var($min_lhs, x); $var($min_rhs, y); $le($min_lhs, $min_rhs) ? $min_lhs : $min_rhs; })
#define max(x, y) ({ $var($min_lhs, x); $var($min_rhs, y); $ge($min_lhs, $min_rhs) ? $min_lhs : $min_rhs; })

#define say(str) CORD_put(CORD_cat(str, "\n"), stdout)
#define $test(src, expr, expected) do { \
        CORD $result = $cord(expr); \
        CORD $output = CORD_catn(5, USE_COLOR ? "\x1b[33;1m>>\x1b[0m " : ">> ", src, USE_COLOR ? "\n\x1b[0;2m=\x1b[m " : "\n= ", $result, "\x1b[m"); \
        puts(CORD_to_const_char_star($output)); \
        if (expected && CORD_cmp($result, expected)) { \
            fprintf(stderr, USE_COLOR ? "\x1b[31;1;7mTEST FAILURE!\x1b[27m\nI expected:\n\t\x1b[0;1m%s\x1b[1;31m\nbut got:\n\t%s\x1b[m\n" : "TEST FAILURE!\nI expected:\n\t%s\nbut got:\n\t%s\n", CORD_to_const_char_star(expected), CORD_to_const_char_star($result)); \
            raise(SIGABRT); \
        } \
    } while (0)

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
