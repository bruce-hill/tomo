#pragma once

// Functions that operate on arrays

#include <stdbool.h>

#include "datatypes.h"
#include "functions.h"
#include "integers.h"
#include "types.h"
#include "util.h"

// Convert negative indices to back-indexed without branching: index0 = index + (index < 0)*(len+1)) - 1
#define Array_get(item_type, arr_expr, index_expr, start, end) *({ \
    const Array_t arr = arr_expr; int64_t index = index_expr; \
    int64_t off = index + (index < 0) * (arr.length + 1) - 1; \
    if (__builtin_expect(off < 0 || off >= arr.length, 0)) \
        fail_source(__SOURCE_FILE__, start, end, "Invalid array index: %s (array has length %ld)\n", Text$as_c_string(Int64$as_text(&index, no, NULL)), arr.length); \
    (item_type*)(arr.data + arr.stride * off);})
#define Array_get_unchecked(type, x, i) *({ const Array_t arr = x; int64_t index = i; \
                                          int64_t off = index + (index < 0) * (arr.length + 1) - 1; \
                                          (type*)(arr.data + arr.stride * off);})
#define Array_lvalue(item_type, arr_expr, index_expr, padded_item_size, start, end) *({ \
    Array_t *arr = arr_expr; int64_t index = index_expr; \
    int64_t off = index + (index < 0) * (arr->length + 1) - 1; \
    if (__builtin_expect(off < 0 || off >= arr->length, 0)) \
        fail_source(__SOURCE_FILE__, start, end, "Invalid array index: %s (array has length %ld)\n", Text$as_c_string(Int64$as_text(&index, no, NULL)), arr->length); \
    if (arr->data_refcount > 0) \
        Array$compact(arr, padded_item_size); \
    (item_type*)(arr->data + arr->stride * off); })
#define Array_lvalue_unchecked(item_type, arr_expr, index_expr, padded_item_size) *({ \
    Array_t *arr = arr_expr; int64_t index = index_expr; \
    int64_t off = index + (index < 0) * (arr->length + 1) - 1; \
    if (arr->data_refcount > 0) \
        Array$compact(arr, padded_item_size); \
    (item_type*)(arr->data + arr->stride * off); })
#define Array_set(item_type, arr, index, value, padded_item_size, start, end) \
    Array_lvalue(item_type, arr_expr, index, padded_item_size, start, end) = value
#define is_atomic(x) _Generic(x, bool: true, int8_t: true, int16_t: true, int32_t: true, int64_t: true, float: true, double: true, default: false)
#define TypedArray(t, ...) ({ t items[] = {__VA_ARGS__}; \
                         (Array_t){.length=sizeof(items)/sizeof(items[0]), \
                         .stride=(int64_t)&items[1] - (int64_t)&items[0], \
                         .data=memcpy(GC_MALLOC(sizeof(items)), items, sizeof(items)), \
                         .atomic=0, \
                         .data_refcount=0}; })
#define TypedArrayN(t, N, ...) ({ t items[N] = {__VA_ARGS__}; \
                         (Array_t){.length=N, \
                         .stride=(int64_t)&items[1] - (int64_t)&items[0], \
                         .data=memcpy(GC_MALLOC(sizeof(items)), items, sizeof(items)), \
                         .atomic=0, \
                         .data_refcount=0}; })
#define Array(x, ...) ({ __typeof(x) items[] = {x, __VA_ARGS__}; \
                         (Array_t){.length=sizeof(items)/sizeof(items[0]), \
                         .stride=(int64_t)&items[1] - (int64_t)&items[0], \
                         .data=memcpy(is_atomic(x) ? GC_MALLOC_ATOMIC(sizeof(items)) : GC_MALLOC(sizeof(items)), items, sizeof(items)), \
                         .atomic=is_atomic(x), \
                         .data_refcount=0}; })
// Array refcounts use a saturating add, where once it's at the max value, it stays there.
#define ARRAY_INCREF(arr) (arr).data_refcount += ((arr).data_refcount < ARRAY_MAX_DATA_REFCOUNT)
#define ARRAY_DECREF(arr) (arr).data_refcount -= ((arr).data_refcount < ARRAY_MAX_DATA_REFCOUNT)
#define ARRAY_COPY(arr) ({ ARRAY_INCREF(arr); arr; })

#define Array$insert_value(arr, item_expr, index, padded_item_size) ({ __typeof(item_expr) item = item_expr; Array$insert(arr, &item, index, padded_item_size); })
void Array$insert(Array_t *arr, const void *item, Int_t index, int64_t padded_item_size);
void Array$insert_all(Array_t *arr, Array_t to_insert, Int_t index, int64_t padded_item_size);
void Array$remove_at(Array_t *arr, Int_t index, Int_t count, int64_t padded_item_size);
void Array$remove_item(Array_t *arr, void *item, Int_t max_removals, const TypeInfo *type);
#define Array$remove_item_value(arr, item_expr, max, type) ({ __typeof(item_expr) item = item_expr; Array$remove_item(arr, &item, max, type); })
Int_t Array$find(Array_t arr, void *item, const TypeInfo *type);
#define Array$find_value(arr, item_expr, type) ({ __typeof(item_expr) item = item_expr; Array$find(arr, &item, type); })
Int_t Array$first(Array_t arr, Closure_t predicate);
void Array$sort(Array_t *arr, Closure_t comparison, int64_t padded_item_size);
Array_t Array$sorted(Array_t arr, Closure_t comparison, int64_t padded_item_size);
void Array$shuffle(Array_t *arr, int64_t padded_item_size);
Array_t Array$shuffled(Array_t arr, int64_t padded_item_size);
void *Array$random(Array_t arr);
#define Array$random_value(arr, t) ({ Array_t _arr = arr; if (_arr.length == 0) fail("Cannot get a random value from an empty array!"); *(t*)Array$random(_arr); })
Array_t Array$sample(Array_t arr, Int_t n, Array_t weights, int64_t padded_item_size);
Table_t Array$counts(Array_t arr, const TypeInfo *type);
void Array$clear(Array_t *array);
void Array$compact(Array_t *arr, int64_t padded_item_size);
PUREFUNC bool Array$has(Array_t array, void *item, const TypeInfo *type);
#define Array$has_value(arr, item_expr, type) ({ __typeof(item_expr) item = item_expr; Array$has(arr, &item, type); })
PUREFUNC Array_t Array$from(Array_t array, Int_t first);
PUREFUNC Array_t Array$to(Array_t array, Int_t last);
PUREFUNC Array_t Array$by(Array_t array, Int_t stride, int64_t padded_item_size);
PUREFUNC Array_t Array$reversed(Array_t array, int64_t padded_item_size);
Array_t Array$concat(Array_t x, Array_t y, int64_t padded_item_size);
PUREFUNC uint64_t Array$hash(const Array_t *arr, const TypeInfo *type);
PUREFUNC int32_t Array$compare(const Array_t *x, const Array_t *y, const TypeInfo *type);
PUREFUNC bool Array$equal(const Array_t *x, const Array_t *y, const TypeInfo *type);
Text_t Array$as_text(const Array_t *arr, bool colorize, const TypeInfo *type);
void Array$heapify(Array_t *heap, Closure_t comparison, int64_t padded_item_size);
void Array$heap_push(Array_t *heap, const void *item, Closure_t comparison, int64_t padded_item_size);
#define Array$heap_push_value(heap, _value, comparison, padded_item_size) ({ __typeof(_value) value = _value; Array$heap_push(heap, &value, comparison, padded_item_size); })
void Array$heap_pop(Array_t *heap, Closure_t comparison, int64_t padded_item_size);
#define Array$heap_pop_value(heap, comparison, padded_item_size, type) \
    ({ Array_t *_heap = heap; if (_heap->length == 0) fail("Attempt to pop from an empty array"); \
     type value = *(type*)_heap->data; Array$heap_pop(_heap, comparison, padded_item_size); value; })
Int_t Array$binary_search(Array_t array, void *target, Closure_t comparison);
#define Array$binary_search_value(array, target, comparison) \
    ({ __typeof(target) _target = target; Array$binary_search(array, &_target, comparison); })

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
