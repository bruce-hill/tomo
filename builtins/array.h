#pragma once
#include <stdbool.h>
#include <gc/cord.h>

#include "../util.h"
#include "datatypes.h"
#include "functions.h"
#include "types.h"

void Array_insert(array_t *arr, const void *item, int64_t index, int64_t item_size);
void Array_insert_all(array_t *arr, array_t to_insert, int64_t index, int64_t item_size);
void Array_remove(array_t *arr, int64_t index, int64_t count, int64_t item_size);
void Array_sort(array_t *arr, const TypeInfo *type);
void Array_shuffle(array_t *arr, int64_t item_size);
void Array_clear(array_t *array);
void Array_compact(array_t *arr, int64_t item_size);
bool Array_contains(array_t array, void *item, const TypeInfo *type);
array_t Array_slice(array_t *array, int64_t first, int64_t stride, int64_t length, bool readonly, const TypeInfo *type);
uint32_t Array_hash(const array_t *arr, const TypeInfo *type);
int32_t Array_compare(const array_t *x, const array_t *y, const TypeInfo *type);
bool Array_equal(const array_t *x, const array_t *y, const TypeInfo *type);
CORD Array_as_str(const array_t *arr, bool colorize, const TypeInfo *type);

// Due to some C language weirdness, the type of "foo" is inferred to be `char[3]` instead of `const char*`
// This is a hacky workaround to ensure that __typeof("foo") => `const char *`
#define FIX_STR_LITERAL(s) _Generic(((void)0, s), char*: (const char*)s, default: s)

#define ARRAY_OF(t) t**
#define EMPTY_ARRAY(t) (t**)new(array_t)
#define LENGTH(arr) (((array_t*)(arr))->length)
#define ARRAY(x, ...) (__typeof(FIX_STR_LITERAL(x))**)new(array_t, \
    .data=memcpy(GC_MALLOC(sizeof((__typeof(FIX_STR_LITERAL(x))[]){x, __VA_ARGS__})), (__typeof(FIX_STR_LITERAL(x))[]){x, __VA_ARGS__}, \
                 sizeof((__typeof(FIX_STR_LITERAL(x))[]){x, __VA_ARGS__})), \
    .length=(sizeof((__typeof(FIX_STR_LITERAL(x))[]){x, __VA_ARGS__})) / sizeof(FIX_STR_LITERAL(x)), \
    .stride=sizeof(FIX_STR_LITERAL(x)))
#define STATIC_ARRAY(x, ...) ((array_t){ \
    .data=(__typeof(FIX_STR_LITERAL(x))[]){x, __VA_ARGS__}, \
    .length=(sizeof((__typeof(FIX_STR_LITERAL(x))[]){x, __VA_ARGS__})) / sizeof(FIX_STR_LITERAL(x)), \
    .stride=sizeof(FIX_STR_LITERAL(x))})
#define foreach(arr, var, last) for (__typeof(arr[0]) var = arr[0], last = ith_addr(arr, LENGTH(arr)-1); var && var <= last; var = ((void*)var) + ((array_t*)(arr))->stride)
#define ith_addr(arr, i) ((__typeof(arr[0]))(((array_t*)(arr))->data + (i)*((array_t*)(arr))->stride))
#define ith(arr, i) (*ith_addr(arr,i))
#define append(arr, obj) Array_insert((array_t*)(arr), (__typeof((arr)[0][0])[]){obj}, 0, sizeof((arr)[0][0]))
#define remove(arr, i) Array_remove((array_t*)(arr), (i)+1, 1, sizeof(arr[0][0]))

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
