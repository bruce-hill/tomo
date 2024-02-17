#pragma once
#include <stdbool.h>
#include <gc/cord.h>

#include "../util.h"
#include "datatypes.h"
#include "functions.h"
#include "types.h"

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
