#pragma once

// Functions that operate on lists

#include <stdbool.h>

#include "datatypes.h"
#include "integers.h"
#include "types.h"
#include "util.h"

// Convert negative indices to back-indexed without branching: index0 = index + (index < 0)*(len+1)) - 1
#define List_get(item_type, arr_expr, index_expr, start, end) *({ \
    const List_t arr = arr_expr; int64_t index = index_expr; \
    int64_t off = index + (index < 0) * (arr.length + 1) - 1; \
    if (unlikely(off < 0 || off >= arr.length)) \
        fail_source(__SOURCE_FILE__, start, end, "Invalid list index: %s (list has length %ld)\n", Text$as_c_string(Int64$as_text(&index, no, NULL)), arr.length); \
    (item_type*)(arr.data + arr.stride * off);})
#define List_get_unchecked(type, x, i) *({ const List_t arr = x; int64_t index = i; \
                                          int64_t off = index + (index < 0) * (arr.length + 1) - 1; \
                                          (type*)(arr.data + arr.stride * off);})
#define List_lvalue(item_type, arr_expr, index_expr, start, end) *({ \
    List_t *arr = arr_expr; int64_t index = index_expr; \
    int64_t off = index + (index < 0) * (arr->length + 1) - 1; \
    if (unlikely(off < 0 || off >= arr->length)) \
        fail_source(__SOURCE_FILE__, start, end, "Invalid list index: %s (list has length %ld)\n", Text$as_c_string(Int64$as_text(&index, no, NULL)), arr->length); \
    if (arr->data_refcount > 0) \
        List$compact(arr, sizeof(item_type)); \
    (item_type*)(arr->data + arr->stride * off); })
#define List_lvalue_unchecked(item_type, arr_expr, index_expr) *({ \
    List_t *arr = arr_expr; int64_t index = index_expr; \
    int64_t off = index + (index < 0) * (arr->length + 1) - 1; \
    if (arr->data_refcount > 0) \
        List$compact(arr, sizeof(item_type)); \
    (item_type*)(arr->data + arr->stride * off); })
#define List_set(item_type, arr, index, value, start, end) \
    List_lvalue(item_type, arr_expr, index, start, end) = value
#define is_atomic(x) _Generic(x, bool: true, int8_t: true, int16_t: true, int32_t: true, int64_t: true, float: true, double: true, default: false)
#define TypedList(t, ...) ({ t items[] = {__VA_ARGS__}; \
                         (List_t){.length=sizeof(items)/sizeof(items[0]), \
                         .stride=(int64_t)&items[1] - (int64_t)&items[0], \
                         .data=memcpy(GC_MALLOC(sizeof(items)), items, sizeof(items)), \
                         .atomic=0, \
                         .data_refcount=0}; })
#define TypedListN(t, N, ...) ({ t items[N] = {__VA_ARGS__}; \
                         (List_t){.length=N, \
                         .stride=(int64_t)&items[1] - (int64_t)&items[0], \
                         .data=memcpy(GC_MALLOC(sizeof(items)), items, sizeof(items)), \
                         .atomic=0, \
                         .data_refcount=0}; })
#define List(x, ...) ({ __typeof(x) items[] = {x, __VA_ARGS__}; \
                         (List_t){.length=sizeof(items)/sizeof(items[0]), \
                         .stride=(int64_t)&items[1] - (int64_t)&items[0], \
                         .data=memcpy(is_atomic(x) ? GC_MALLOC_ATOMIC(sizeof(items)) : GC_MALLOC(sizeof(items)), items, sizeof(items)), \
                         .atomic=is_atomic(x), \
                         .data_refcount=0}; })
// List refcounts use a saturating add, where once it's at the max value, it stays there.
#define LIST_INCREF(arr) (arr).data_refcount += ((arr).data_refcount < LIST_MAX_DATA_REFCOUNT)
#define LIST_DECREF(arr) (arr).data_refcount -= ((arr).data_refcount < LIST_MAX_DATA_REFCOUNT)
#define LIST_COPY(arr) ({ LIST_INCREF(arr); arr; })

#define List$insert_value(arr, item_expr, index, padded_item_size) List$insert(arr, (__typeof(item_expr)[1]){item_expr}, index, padded_item_size)
void List$insert(List_t *arr, const void *item, Int_t index, int64_t padded_item_size);
void List$insert_all(List_t *arr, List_t to_insert, Int_t index, int64_t padded_item_size);
void List$remove_at(List_t *arr, Int_t index, Int_t count, int64_t padded_item_size);
void List$remove_item(List_t *arr, void *item, Int_t max_removals, const TypeInfo_t *type);
#define List$remove_item_value(arr, item_expr, max, type) List$remove_item(arr, (__typeof(item_expr)[1]){item_expr}, max, type)

#define List$pop(arr_expr, index_expr, item_type, nonnone_var, nonnone_expr, none_expr) ({ \
    List_t *arr = arr_expr; \
    Int_t index = index_expr; \
    int64_t index64 = Int64$from_int(index, false); \
    int64_t off = index64 + (index64 < 0) * (arr->length + 1) - 1; \
    (off >= 0 && off < arr->length) ? ({ \
        item_type nonnone_var = *(item_type*)(arr->data + off*arr->stride); \
        List$remove_at(arr, index, I_small(1), sizeof(item_type)); \
        nonnone_expr; \
    }) : none_expr; })

OptionalInt_t List$find(List_t arr, void *item, const TypeInfo_t *type);
#define List$find_value(arr, item_expr, type) ({ __typeof(item_expr) item = item_expr; List$find(arr, &item, type); })
OptionalInt_t List$first(List_t arr, Closure_t predicate);
void List$sort(List_t *arr, Closure_t comparison, int64_t padded_item_size);
List_t List$sorted(List_t arr, Closure_t comparison, int64_t padded_item_size);
void List$shuffle(List_t *arr, RNG_t rng, int64_t padded_item_size);
List_t List$shuffled(List_t arr, RNG_t rng, int64_t padded_item_size);
void *List$random(List_t arr, RNG_t rng);
#define List$random_value(arr, rng, t) ({ List_t _arr = arr; if (_arr.length == 0) fail("Cannot get a random value from an empty list!"); *(t*)List$random(_arr, rng); })
List_t List$sample(List_t arr, Int_t n, List_t weights, RNG_t rng, int64_t padded_item_size);
Table_t List$counts(List_t arr, const TypeInfo_t *type);
void List$clear(List_t *list);
void List$compact(List_t *arr, int64_t padded_item_size);
PUREFUNC bool List$has(List_t list, void *item, const TypeInfo_t *type);
#define List$has_value(arr, item_expr, type) ({ __typeof(item_expr) item = item_expr; List$has(arr, &item, type); })
PUREFUNC List_t List$from(List_t list, Int_t first);
PUREFUNC List_t List$to(List_t list, Int_t last);
PUREFUNC List_t List$by(List_t list, Int_t stride, int64_t padded_item_size);
PUREFUNC List_t List$slice(List_t list, Int_t int_first, Int_t int_last);
PUREFUNC List_t List$reversed(List_t list, int64_t padded_item_size);
List_t List$concat(List_t x, List_t y, int64_t padded_item_size);
PUREFUNC uint64_t List$hash(const void *arr, const TypeInfo_t *type);
PUREFUNC int32_t List$compare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool List$equal(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool List$is_none(const void *obj, const TypeInfo_t*);
Text_t List$as_text(const void *arr, bool colorize, const TypeInfo_t *type);
void List$heapify(List_t *heap, Closure_t comparison, int64_t padded_item_size);
void List$heap_push(List_t *heap, const void *item, Closure_t comparison, int64_t padded_item_size);
#define List$heap_push_value(heap, _value, comparison, padded_item_size) ({ __typeof(_value) value = _value; List$heap_push(heap, &value, comparison, padded_item_size); })
void List$heap_pop(List_t *heap, Closure_t comparison, int64_t padded_item_size);
#define List$heap_pop_value(heap, comparison, type, nonnone_var, nonnone_expr, none_expr) \
    ({ List_t *_heap = heap; \
     (_heap->length > 0) ? ({ \
         type nonnone_var = *(type*)_heap->data; \
         List$heap_pop(_heap, comparison, sizeof(type)); \
         nonnone_expr; \
     }) : none_expr; })
Int_t List$binary_search(List_t list, void *target, Closure_t comparison);
#define List$binary_search_value(list, target, comparison) \
    ({ __typeof(target) _target = target; List$binary_search(list, &_target, comparison); })
void List$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type);
void List$deserialize(FILE *in, void *obj, List_t *pointers, const TypeInfo_t *type);

#define List$metamethods ((metamethods_t){ \
    .as_text=List$as_text, \
    .compare=List$compare, \
    .equal=List$equal, \
    .hash=List$hash, \
    .is_none=List$is_none, \
    .serialize=List$serialize, \
    .deserialize=List$deserialize, \
})

#define List$info(item_info) &((TypeInfo_t){.size=sizeof(List_t), .align=__alignof__(List_t), \
                                .tag=ListInfo, .ListInfo.item=item_info, \
                                .metamethods=List$metamethods})

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
