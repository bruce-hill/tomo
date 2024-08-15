#pragma once

// Functions that operate on arrays

#include <stdbool.h>
#include <gc/cord.h>

#include "datatypes.h"
#include "functions.h"
#include "integers.h"
#include "types.h"
#include "util.h"

// Convert negative indices to back-indexed without branching: index0 = index + (index < 0)*(len+1)) - 1
#define Array_get(item_type, arr_expr, index_expr, filename, start, end) *({ \
    const array_t arr = arr_expr; int64_t index = Int_to_Int64(index_expr, false); \
    int64_t off = index + (index < 0) * (arr.length + 1) - 1; \
    if (__builtin_expect(off < 0 || off >= arr.length, 0)) \
        fail_source(filename, start, end, "Invalid array index: %r (array has length %ld)\n", Int64$as_text(&index, no, NULL), arr.length); \
    (item_type*)(arr.data + arr.stride * off);})
#define Array_lvalue(item_type, arr_expr, index_expr, padded_item_size, filename, start, end) *({ \
    array_t *arr = arr_expr; int64_t index = Int_to_Int64(index_expr, false); \
    int64_t off = index + (index < 0) * (arr->length + 1) - 1; \
    if (__builtin_expect(off < 0 || off >= arr->length, 0)) \
        fail_source(filename, start, end, "Invalid array index: %r (array has length %ld)\n", Int64$as_text(&index, no, NULL), arr->length); \
    if (arr->data_refcount > 0) \
        Array$compact(arr, padded_item_size); \
    (item_type*)(arr->data + arr->stride * off); })
#define Array_set(item_type, arr, index, value, padded_item_size, filename, start, end) \
    Array_lvalue(item_type, arr_expr, index, padded_item_size, filename, start, end) = value
#define Array_get_unchecked(type, x, i) *({ const array_t arr = x; int64_t index = I(i); \
                                          int64_t off = index + (index < 0) * (arr.length + 1) - 1; \
                                          (type*)(arr.data + arr.stride * off);})
#define is_atomic(x) _Generic(x, bool: true, int8_t: true, int16_t: true, int32_t: true, int64_t: true, float: true, double: true, default: false)
#define TypedArray(t, ...) ({ t items[] = {__VA_ARGS__}; \
                         (array_t){.length=sizeof(items)/sizeof(items[0]), \
                         .stride=(int64_t)&items[1] - (int64_t)&items[0], \
                         .data=memcpy(GC_MALLOC(sizeof(items)), items, sizeof(items)), \
                         .atomic=0, \
                         .data_refcount=0}; })
#define TypedArrayN(t, N, ...) ({ t items[N] = {__VA_ARGS__}; \
                         (array_t){.length=N, \
                         .stride=(int64_t)&items[1] - (int64_t)&items[0], \
                         .data=memcpy(GC_MALLOC(sizeof(items)), items, sizeof(items)), \
                         .atomic=0, \
                         .data_refcount=0}; })
#define Array(x, ...) ({ __typeof(x) items[] = {x, __VA_ARGS__}; \
                         (array_t){.length=sizeof(items)/sizeof(items[0]), \
                         .stride=(int64_t)&items[1] - (int64_t)&items[0], \
                         .data=memcpy(is_atomic(x) ? GC_MALLOC_ATOMIC(sizeof(items)) : GC_MALLOC(sizeof(items)), items, sizeof(items)), \
                         .atomic=is_atomic(x), \
                         .data_refcount=0}; })
// Array refcounts use a saturating add, where once it's at the max value, it stays there.
#define ARRAY_INCREF(arr) (arr).data_refcount += ((arr).data_refcount < ARRAY_MAX_DATA_REFCOUNT)
#define ARRAY_DECREF(arr) (arr).data_refcount -= ((arr).data_refcount < ARRAY_MAX_DATA_REFCOUNT)
#define ARRAY_COPY(arr) ({ ARRAY_INCREF(arr); arr; })

#define Array$insert_value(arr, item_expr, index, padded_item_size) ({ __typeof(item_expr) item = item_expr; Array$insert(arr, &item, index, padded_item_size); })
void Array$insert(array_t *arr, const void *item, Int_t index, int64_t padded_item_size);
void Array$insert_all(array_t *arr, array_t to_insert, Int_t index, int64_t padded_item_size);
void Array$remove(array_t *arr, Int_t index, Int_t count, int64_t padded_item_size);
void Array$sort(array_t *arr, closure_t comparison, int64_t padded_item_size);
array_t Array$sorted(array_t arr, closure_t comparison, int64_t padded_item_size);
void Array$shuffle(array_t *arr, int64_t padded_item_size);
void *Array$random(array_t arr);
#define Array$random_value(arr, t) ({ array_t _arr = arr; if (_arr.length == 0) fail("Cannot get a random value from an empty array!"); *(t*)Array$random(_arr); })
array_t Array$sample(array_t arr, Int_t n, array_t weights, int64_t padded_item_size);
table_t Array$counts(array_t arr, const TypeInfo *type);
void Array$clear(array_t *array);
void Array$compact(array_t *arr, int64_t padded_item_size);
bool Array$contains(array_t array, void *item, const TypeInfo *type);
array_t Array$from(array_t array, Int_t first);
array_t Array$to(array_t array, Int_t last);
array_t Array$by(array_t array, Int_t stride, int64_t padded_item_size);
array_t Array$reversed(array_t array, int64_t padded_item_size);
array_t Array$concat(array_t x, array_t y, int64_t padded_item_size);
uint32_t Array$hash(const array_t *arr, const TypeInfo *type);
int32_t Array$compare(const array_t *x, const array_t *y, const TypeInfo *type);
bool Array$equal(const array_t *x, const array_t *y, const TypeInfo *type);
CORD Array$as_text(const array_t *arr, bool colorize, const TypeInfo *type);
void Array$heapify(array_t *heap, closure_t comparison, int64_t padded_item_size);
void Array$heap_push(array_t *heap, const void *item, closure_t comparison, int64_t padded_item_size);
#define Array$heap_push_value(heap, _value, comparison, padded_item_size) ({ __typeof(_value) value = _value; Array$heap_push(heap, &value, comparison, padded_item_size); })
void Array$heap_pop(array_t *heap, closure_t comparison, int64_t padded_item_size);
#define Array$heap_pop_value(heap, comparison, padded_item_size, type) \
    ({ array_t *_heap = heap; if (_heap->length == 0) fail("Attempt to pop from an empty array"); \
     type value = *(type*)_heap->data; Array$heap_pop(_heap, comparison, padded_item_size); value; })
Int_t Array$binary_search(array_t array, void *target, closure_t comparison);
#define Array$binary_search_value(array, target, comparison) \
    ({ __typeof(target) _target = target; Array$binary_search(array, &_target, comparison); })

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
