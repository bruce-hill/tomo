#pragma once
#include <stdbool.h>
#include <gc/cord.h>

#include "../util.h"
#include "datatypes.h"
#include "functions.h"
#include "types.h"

// Convert negative indices to back-indexed without branching: index0 = index + (index < 0)*(len+1)) - 1
#define $Array_get(type, x, i) ({ const array_t *$arr = x; int64_t $index = (int64_t)(i); \
                                int64_t $off = $index + ($index < 0) * ($arr->length + 1) - 1; \
                                if (__builtin_expect($off < 0 && $off >= $arr->length, 0)) \
                                    fail("Invalid array index: %ld (array has length %ld)", $index, $arr->length); \
                                *(type*)($arr->data + $arr->stride * $off);})
#define $Array_get_unchecked(type, x, i) ({ const array_t *$arr = x; int64_t $index = (int64_t)(i); \
                                          int64_t $off = $index + ($index < 0) * ($arr->length + 1) - 1; \
                                          *(type*)($arr->data + $arr->stride * $off);})
#define $Array(x, ...) ({ __typeof(x) $items[] = {x, __VA_ARGS__}; \
                         (array_t){.length=sizeof($items)/sizeof($items[0]), \
                         .stride=(int64_t)&$items[1] - (int64_t)&$items[0], \
                         .data=memcpy(GC_MALLOC(sizeof($items)), $items,  sizeof($items)), \
                         .copy_on_write=1}; })

void Array__insert(array_t *arr, const void *item, int64_t index, const TypeInfo *type);
void Array__insert_all(array_t *arr, array_t to_insert, int64_t index, const TypeInfo *type);
void Array__remove(array_t *arr, int64_t index, int64_t count, const TypeInfo *type);
void Array__sort(array_t *arr, const TypeInfo *type);
void Array__shuffle(array_t *arr, const TypeInfo *type);
void Array__clear(array_t *array, const TypeInfo *type);
void Array__compact(array_t *arr, const TypeInfo *type);
bool Array__contains(array_t array, void *item, const TypeInfo *type);
array_t Array__slice(array_t *array, int64_t first, int64_t stride, int64_t length, bool readonly, const TypeInfo *type);
uint32_t Array__hash(const array_t *arr, const TypeInfo *type);
int32_t Array__compare(const array_t *x, const array_t *y, const TypeInfo *type);
bool Array__equal(const array_t *x, const array_t *y, const TypeInfo *type);
CORD Array__as_str(const array_t *arr, bool colorize, const TypeInfo *type);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
