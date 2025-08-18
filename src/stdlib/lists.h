#pragma once

// Functions that operate on lists

#include <stdbool.h>

#include "datatypes.h"
#include "integers.h"
#include "types.h"
#include "util.h"

// Convert negative indices to back-indexed without branching: index0 = index + (index < 0)*(len+1)) - 1
#define List_get(item_type, arr_expr, index_expr, start, end) *({ \
    const List_t list = arr_expr; int64_t index = index_expr; \
    int64_t off = index + (index < 0) * (list.length + 1) - 1; \
    if (unlikely(off < 0 || off >= list.length)) \
        fail_source(__SOURCE_FILE__, start, end, "Invalid list index: ", index, " (list has length ", (int64_t)list.length, ")\n"); \
    (item_type*)(list.data + list.stride * off);})
#define List_get_unchecked(type, x, i) *({ const List_t list = x; int64_t index = i; \
                                          int64_t off = index + (index < 0) * (list.length + 1) - 1; \
                                          (type*)(list.data + list.stride * off);})
#define List_lvalue(item_type, arr_expr, index_expr, start, end) *({ \
    List_t *list = arr_expr; int64_t index = index_expr; \
    int64_t off = index + (index < 0) * (list->length + 1) - 1; \
    if (unlikely(off < 0 || off >= list->length)) \
        fail_source(__SOURCE_FILE__, start, end, "Invalid list index: ", index, " (list has length ", (int64_t)list->length, ")\n"); \
    if (list->data_refcount > 0) \
        Listヽcompact(list, sizeof(item_type)); \
    (item_type*)(list->data + list->stride * off); })
#define List_lvalue_unchecked(item_type, arr_expr, index_expr) *({ \
    List_t *list = arr_expr; int64_t index = index_expr; \
    int64_t off = index + (index < 0) * (list->length + 1) - 1; \
    if (list->data_refcount > 0) \
        Listヽcompact(list, sizeof(item_type)); \
    (item_type*)(list->data + list->stride * off); })
#define List_set(item_type, list, index, value, start, end) \
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
#define LIST_INCREF(list) (list).data_refcount += ((list).data_refcount < LIST_MAX_DATA_REFCOUNT)
#define LIST_DECREF(list) (list).data_refcount -= ((list).data_refcount < LIST_MAX_DATA_REFCOUNT)
#define LIST_COPY(list) ({ LIST_INCREF(list); list; })

#define Listヽinsert_value(list, item_expr, index, padded_item_size) Listヽinsert(list, (__typeof(item_expr)[1]){item_expr}, index, padded_item_size)
void Listヽinsert(List_t *list, const void *item, Int_t index, int64_t padded_item_size);
void Listヽinsert_all(List_t *list, List_t to_insert, Int_t index, int64_t padded_item_size);
void Listヽremove_at(List_t *list, Int_t index, Int_t count, int64_t padded_item_size);
void Listヽremove_item(List_t *list, void *item, Int_t max_removals, const TypeInfo_t *type);
#define Listヽremove_item_value(list, item_expr, max, type) Listヽremove_item(list, (__typeof(item_expr)[1]){item_expr}, max, type)

#define Listヽpop(arr_expr, index_expr, item_type, nonnone_var, nonnone_expr, none_expr) ({ \
    List_t *list = arr_expr; \
    Int_t index = index_expr; \
    int64_t index64 = Int64ヽfrom_int(index, false); \
    int64_t off = index64 + (index64 < 0) * (list->length + 1) - 1; \
    (off >= 0 && off < list->length) ? ({ \
        item_type nonnone_var = *(item_type*)(list->data + off*list->stride); \
        Listヽremove_at(list, index, I_small(1), sizeof(item_type)); \
        nonnone_expr; \
    }) : none_expr; })

OptionalInt_t Listヽfind(List_t list, void *item, const TypeInfo_t *type);
#define Listヽfind_value(list, item_expr, type) ({ __typeof(item_expr) item = item_expr; Listヽfind(list, &item, type); })
OptionalInt_t Listヽfirst(List_t list, Closure_t predicate);
void Listヽsort(List_t *list, Closure_t comparison, int64_t padded_item_size);
List_t Listヽsorted(List_t list, Closure_t comparison, int64_t padded_item_size);
void Listヽshuffle(List_t *list, OptionalClosure_t random_int64, int64_t padded_item_size);
List_t Listヽshuffled(List_t list, OptionalClosure_t random_int64, int64_t padded_item_size);
void *Listヽrandom(List_t list, OptionalClosure_t random_int64);
#define Listヽrandom_value(list, random_int64, t) ({ List_t _arr = list; if (_arr.length == 0) fail("Cannot get a random value from an empty list!"); *(t*)Listヽrandom(_arr, random_int64); })
List_t Listヽsample(List_t list, Int_t n, List_t weights, Closure_t random_num, int64_t padded_item_size);
Table_t Listヽcounts(List_t list, const TypeInfo_t *type);
void Listヽclear(List_t *list);
void Listヽcompact(List_t *list, int64_t padded_item_size);
PUREFUNC bool Listヽhas(List_t list, void *item, const TypeInfo_t *type);
#define Listヽhas_value(list, item_expr, type) ({ __typeof(item_expr) item = item_expr; Listヽhas(list, &item, type); })
PUREFUNC List_t Listヽfrom(List_t list, Int_t first);
PUREFUNC List_t Listヽto(List_t list, Int_t last);
PUREFUNC List_t Listヽby(List_t list, Int_t stride, int64_t padded_item_size);
PUREFUNC List_t Listヽslice(List_t list, Int_t int_first, Int_t int_last);
PUREFUNC List_t Listヽreversed(List_t list, int64_t padded_item_size);
List_t Listヽconcat(List_t x, List_t y, int64_t padded_item_size);
PUREFUNC uint64_t Listヽhash(const void *list, const TypeInfo_t *type);
PUREFUNC int32_t Listヽcompare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Listヽequal(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Listヽis_none(const void *obj, const TypeInfo_t*);
Text_t Listヽas_text(const void *list, bool colorize, const TypeInfo_t *type);
void Listヽheapify(List_t *heap, Closure_t comparison, int64_t padded_item_size);
void Listヽheap_push(List_t *heap, const void *item, Closure_t comparison, int64_t padded_item_size);
#define Listヽheap_push_value(heap, _value, comparison, padded_item_size) ({ __typeof(_value) value = _value; Listヽheap_push(heap, &value, comparison, padded_item_size); })
void Listヽheap_pop(List_t *heap, Closure_t comparison, int64_t padded_item_size);
#define Listヽheap_pop_value(heap, comparison, type, nonnone_var, nonnone_expr, none_expr) \
    ({ List_t *_heap = heap; \
     (_heap->length > 0) ? ({ \
         type nonnone_var = *(type*)_heap->data; \
         Listヽheap_pop(_heap, comparison, sizeof(type)); \
         nonnone_expr; \
     }) : none_expr; })
Int_t Listヽbinary_search(List_t list, void *target, Closure_t comparison);
#define Listヽbinary_search_value(list, target, comparison) \
    ({ __typeof(target) _target = target; Listヽbinary_search(list, &_target, comparison); })
void Listヽserialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type);
void Listヽdeserialize(FILE *in, void *obj, List_t *pointers, const TypeInfo_t *type);

#define Listヽmetamethods { \
    .as_text=Listヽas_text, \
    .compare=Listヽcompare, \
    .equal=Listヽequal, \
    .hash=Listヽhash, \
    .is_none=Listヽis_none, \
    .serialize=Listヽserialize, \
    .deserialize=Listヽdeserialize, \
}

#define Listヽinfo(item_info) &((TypeInfo_t){.size=sizeof(List_t), .align=__alignof__(List_t), \
                                .tag=ListInfo, .ListInfo.item=item_info, \
                                .metamethods=Listヽmetamethods})

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
