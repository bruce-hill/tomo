#pragma once

// Functions that operate on arrays

#include <stdbool.h>

#include "datatypes.h"
#include "integers.h"
#include "types.h"
#include "util.h"

// Convert negative indices to back-indexed without branching: index0 = index + (index < 0)*(len+1)) - 1
#define Array_get(item_type, arr_expr, index_expr, start, end) *({ \
    const Array_t arr = arr_expr; int64_t index = index_expr; \
    int64_t off = index + (index < 0) * (arr.length + 1) - 1; \
    if (unlikely(off < 0 || off >= arr.length)) \
        fail_source(__SOURCE_FILE__, start, end, "Invalid array index: ", index, " (array has length ", (int64_t)arr.length, ")\n"); \
    (item_type*)(arr.data + arr.stride * off);})
#define Array_get_unchecked(type, x, i) *({ const Array_t arr = x; int64_t index = i; \
                                          int64_t off = index + (index < 0) * (arr.length + 1) - 1; \
                                          (type*)(arr.data + arr.stride * off);})
#define Array_lvalue(item_type, arr_expr, index_expr, start, end) *({ \
    Array_t *arr = arr_expr; int64_t index = index_expr; \
    int64_t off = index + (index < 0) * (arr->length + 1) - 1; \
    if (unlikely(off < 0 || off >= arr->length)) \
        fail_source(__SOURCE_FILE__, start, end, "Invalid array index: ", index, " (array has length ", (int64_t)arr->length, ")\n"); \
    if (arr->data_refcount > 0) \
        Array$compact(arr, sizeof(item_type)); \
    (item_type*)(arr->data + arr->stride * off); })
#define Array_lvalue_unchecked(item_type, arr_expr, index_expr) *({ \
    Array_t *arr = arr_expr; int64_t index = index_expr; \
    int64_t off = index + (index < 0) * (arr->length + 1) - 1; \
    if (arr->data_refcount > 0) \
        Array$compact(arr, sizeof(item_type)); \
    (item_type*)(arr->data + arr->stride * off); })
#define Array_set(item_type, arr, index, value, start, end) \
    Array_lvalue(item_type, arr_expr, index, start, end) = value
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

#define Array$insert_value(arr, item_expr, index, padded_item_size) Array$insert(arr, (__typeof(item_expr)[1]){item_expr}, index, padded_item_size)
void Array$insert(Array_t *arr, const void *item, Int_t index, int64_t padded_item_size);
void Array$insert_all(Array_t *arr, Array_t to_insert, Int_t index, int64_t padded_item_size);
void Array$remove_at(Array_t *arr, Int_t index, Int_t count, int64_t padded_item_size);
void Array$remove_item(Array_t *arr, void *item, Int_t max_removals, const TypeInfo_t *type);
#define Array$remove_item_value(arr, item_expr, max, type) Array$remove_item(arr, (__typeof(item_expr)[1]){item_expr}, max, type)

#define Array$pop(arr_expr, index_expr, item_type, nonnone_var, nonnone_expr, none_expr) ({ \
    Array_t *arr = arr_expr; \
    Int_t index = index_expr; \
    int64_t index64 = Int64$from_int(index, false); \
    int64_t off = index64 + (index64 < 0) * (arr->length + 1) - 1; \
    (off >= 0 && off < arr->length) ? ({ \
        item_type nonnone_var = *(item_type*)(arr->data + off*arr->stride); \
        Array$remove_at(arr, index, I_small(1), sizeof(item_type)); \
        nonnone_expr; \
    }) : none_expr; })

OptionalInt_t Array$find(Array_t arr, void *item, const TypeInfo_t *type);
#define Array$find_value(arr, item_expr, type) ({ __typeof(item_expr) item = item_expr; Array$find(arr, &item, type); })
OptionalInt_t Array$first(Array_t arr, Closure_t predicate);
void Array$sort(Array_t *arr, Closure_t comparison, int64_t padded_item_size);
Array_t Array$sorted(Array_t arr, Closure_t comparison, int64_t padded_item_size);
void Array$shuffle(Array_t *arr, OptionalClosure_t random_int64, int64_t padded_item_size);
Array_t Array$shuffled(Array_t arr, OptionalClosure_t random_int64, int64_t padded_item_size);
void *Array$random(Array_t arr, OptionalClosure_t random_int64);
#define Array$random_value(arr, random_int64, t) ({ Array_t _arr = arr; if (_arr.length == 0) fail("Cannot get a random value from an empty array!"); *(t*)Array$random(_arr, random_int64); })
Array_t Array$sample(Array_t arr, Int_t n, Array_t weights, Closure_t random_num, int64_t padded_item_size);
Table_t Array$counts(Array_t arr, const TypeInfo_t *type);
void Array$clear(Array_t *array);
void Array$compact(Array_t *arr, int64_t padded_item_size);
PUREFUNC bool Array$has(Array_t array, void *item, const TypeInfo_t *type);
#define Array$has_value(arr, item_expr, type) ({ __typeof(item_expr) item = item_expr; Array$has(arr, &item, type); })
PUREFUNC Array_t Array$from(Array_t array, Int_t first);
PUREFUNC Array_t Array$to(Array_t array, Int_t last);
PUREFUNC Array_t Array$by(Array_t array, Int_t stride, int64_t padded_item_size);
PUREFUNC Array_t Array$slice(Array_t array, Int_t int_first, Int_t int_last);
PUREFUNC Array_t Array$reversed(Array_t array, int64_t padded_item_size);
Array_t Array$concat(Array_t x, Array_t y, int64_t padded_item_size);
PUREFUNC uint64_t Array$hash(const void *arr, const TypeInfo_t *type);
PUREFUNC int32_t Array$compare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Array$equal(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Array$is_none(const void *obj, const TypeInfo_t*);
Text_t Array$as_text(const void *arr, bool colorize, const TypeInfo_t *type);
void Array$heapify(Array_t *heap, Closure_t comparison, int64_t padded_item_size);
void Array$heap_push(Array_t *heap, const void *item, Closure_t comparison, int64_t padded_item_size);
#define Array$heap_push_value(heap, _value, comparison, padded_item_size) ({ __typeof(_value) value = _value; Array$heap_push(heap, &value, comparison, padded_item_size); })
void Array$heap_pop(Array_t *heap, Closure_t comparison, int64_t padded_item_size);
#define Array$heap_pop_value(heap, comparison, type, nonnone_var, nonnone_expr, none_expr) \
    ({ Array_t *_heap = heap; \
     (_heap->length > 0) ? ({ \
         type nonnone_var = *(type*)_heap->data; \
         Array$heap_pop(_heap, comparison, sizeof(type)); \
         nonnone_expr; \
     }) : none_expr; })
Int_t Array$binary_search(Array_t array, void *target, Closure_t comparison);
#define Array$binary_search_value(array, target, comparison) \
    ({ __typeof(target) _target = target; Array$binary_search(array, &_target, comparison); })
void Array$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type);
void Array$deserialize(FILE *in, void *obj, Array_t *pointers, const TypeInfo_t *type);

#define Array$metamethods { \
    .as_text=Array$as_text, \
    .compare=Array$compare, \
    .equal=Array$equal, \
    .hash=Array$hash, \
    .is_none=Array$is_none, \
    .serialize=Array$serialize, \
    .deserialize=Array$deserialize, \
}

#define Array$info(item_info) &((TypeInfo_t){.size=sizeof(Array_t), .align=__alignof__(Array_t), \
                                .tag=ArrayInfo, .ArrayInfo.item=item_info, \
                                .metamethods=Array$metamethods})

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
