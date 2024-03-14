#pragma once

#include <gc.h>
#include <gc/cord.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define $heap(x) (__typeof(x)*)memcpy(GC_MALLOC(sizeof(x)), (__typeof(x)[1]){x}, sizeof(x))
#define $stack(x) (__typeof(x)*)((__typeof(x)[1]){x})
#define $tagged(obj_expr, type_name, tag_name) ({ __typeof(obj_expr) $obj = obj_expr; \
                                                $obj.$tag == $tag$##type_name##$##tag_name ? &$obj.tag_name : NULL; })
