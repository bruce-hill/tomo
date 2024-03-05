#pragma once

#include <err.h>
#include <gc.h>
#include <gc/cord.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "SipHash/halfsiphash.h"
#include "builtins/array.h"
#include "builtins/bool.h"
#include "builtins/color.h"
#include "builtins/datatypes.h"
#include "builtins/functions.h"
#include "builtins/integers.h"
#include "builtins/memory.h"
#include "builtins/nums.h"
#include "builtins/pointer.h"
#include "builtins/table.h"
#include "builtins/text.h"
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
#define $cmp(x, y, info) (_Generic(x, int8_t: (x>0)-(y>0), int16_t: (x>0)-(y>0), int32_t: (x>0)-(y>0), int64_t: (x>0)-(y>0), bool: (x>0)-(y>0), \
                                 CORD: CORD_cmp((CORD)x, (CORD)y), char*: strcmp((char*)x, (char*)y), default: generic_compare($stack(x), $stack(y), info)))
#define min(c_type, x, y, info) ({ c_type $lhs = x, $rhs = y; generic_compare(&$lhs, &$rhs, info) <= 0 ? $lhs : $rhs; })
#define max(c_type, x, y, info) ({ c_type $lhs = x, $rhs = y; generic_compare(&$lhs, &$rhs, info) >= 0 ? $lhs : $rhs; })

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
