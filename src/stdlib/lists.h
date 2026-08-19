// Functions that operate on lists

#pragma once

#include <stdbool.h>

#include "datatypes.h"
#include "integers.h"
#include "types.h"
#include "util.h"

extern char _EMPTY_LIST_SENTINEL;
#define EMPTY_LIST ((List_t){.data = &_EMPTY_LIST_SENTINEL})
#define EMPTY_ATOMIC_LIST ((List_t){.data = &_EMPTY_LIST_SENTINEL, .atomic = 1})

// Convert a (possibly negative, 1-indexed) list index to a checked 0-based
// offset without branching on sign: off = index + (index<0)*(len+1) - 1.
// Fails via fail_source if out of bounds. Shared by every accessor below so
// the bounds-check-and-error-message logic lives in exactly one place.
// `index_expr` is evaluated exactly once; `length_val` may be evaluated
// twice, so it must be side-effect-free (every call site below passes a
// struct field read or a plain local, never an expression with side effects).
#define List_checked_offset(index_expr, length_val, start, end)                                                        \
    ({                                                                                                                 \
        int64_t index = index_expr;                                                                                    \
        int64_t off = index + (index < 0) * ((length_val) + 1) - 1;                                                    \
        if (unlikely(off < 0 || off >= (length_val)))                                                                  \
            fail_source(__SOURCE_FILE__, start, end,                                                                   \
                        Text$concat(Text("Invalid list index: "), convert_to_text(index), Text(" (list has length "),  \
                                    convert_to_text((int64_t)(length_val)), Text(")\n")));                             \
        off;                                                                                                           \
    })
#define List_get_checked(list_expr, index_expr, item_type, start, end)                                                 \
    ({                                                                                                                 \
        const List_t list = list_expr;                                                                                 \
        int64_t off = List_checked_offset(index_expr, list.length, start, end);                                        \
        *(item_type *)(list.data + list.stride * off);                                                                 \
    })
#define List_get(list_expr, index_expr, item_type, var, optional_expr, none_expr)                                      \
    ({                                                                                                                 \
        const List_t list = list_expr;                                                                                 \
        int64_t index = index_expr;                                                                                    \
        int64_t offset = index + (index < 0) * (list.length + 1) - 1;                                                  \
        unlikely(offset < 0 || offset >= list.length) ? none_expr : ({                                                 \
            item_type var = *(item_type *)(list.data + list.stride * offset);                                          \
            optional_expr;                                                                                             \
        });                                                                                                            \
    })
#define List_lvalue(item_type, list_expr, index_expr, start, end)                                                      \
    *({                                                                                                                \
        List_t *list = list_expr;                                                                                      \
        int64_t off = List_checked_offset(index_expr, list->length, start, end);                                       \
        if (list->data_refcount > 0) List$compact(list, sizeof(item_type));                                           \
        (item_type *)(list->data + list->stride * off);                                                               \
    })
// Hoisted-header list accessors: variants of List_get_checked / List_lvalue /
// List_swap that take the list's data pointer, stride, and length as values
// (C locals) instead of re-reading them through the list struct on every
// access. Only emitted by the compiler inside loops where static analysis
// proved the body cannot create any new value snapshot of (or resize) the
// list (see cow_hoist_env in compile/loops.c): the loop performs one
// compact-if-shared up front, captures the header fields into locals, and
// compiles accesses against those. This both removes the per-write CoW guard
// (data_refcount provably stays 0) and lets the C compiler keep the header in
// registers / strength-reduce the stride multiply -- re-reading through the
// pointer would force a reload on every iteration, since element stores could
// alias the list struct. Bounds checks stay.
#define List_get_hoisted(data_val, stride_val, length_val, index_expr, item_type, start, end)                          \
    ({                                                                                                                 \
        int64_t off = List_checked_offset(index_expr, length_val, start, end);                                         \
        *(item_type *)((data_val) + (stride_val) * off);                                                               \
    })
#define List_lvalue_hoisted(item_type, data_val, stride_val, length_val, index_expr, start, end)                       \
    (*({                                                                                                               \
        int64_t off = List_checked_offset(index_expr, length_val, start, end);                                         \
        (item_type *)((data_val) + (stride_val) * off);                                                               \
    }))
// `xs.swap(i, j)`: exchange two elements in place, checking each index with
// List_checked_offset in turn (so a failure names whichever index -- i first,
// then j -- was actually out of range). One copy-on-write check covers both
// writes (compare a two-element multi-assignment swap: 4 bounds checks + 2
// CoW checks). Swapping an index with itself is a no-op, not an error.
#define List_swap_hoisted(item_type, data_val, stride_val, length_val, i_expr, j_expr, start, end)                     \
    ({                                                                                                                 \
        int64_t i_off = List_checked_offset(i_expr, length_val, start, end);                                          \
        int64_t j_off = List_checked_offset(j_expr, length_val, start, end);                                          \
        item_type *i_ptr = (item_type *)((data_val) + (stride_val) * i_off);                                           \
        item_type *j_ptr = (item_type *)((data_val) + (stride_val) * j_off);                                           \
        item_type tmp = *i_ptr;                                                                                        \
        *i_ptr = *j_ptr;                                                                                               \
        *j_ptr = tmp;                                                                                                  \
        (void)0;                                                                                                       \
    })
#define List_swap(item_type, list_expr, i_expr, j_expr, start, end)                                                    \
    ({                                                                                                                 \
        List_t *list = list_expr;                                                                                      \
        int64_t i_off = List_checked_offset(i_expr, list->length, start, end);                                        \
        int64_t j_off = List_checked_offset(j_expr, list->length, start, end);                                        \
        if (list->data_refcount > 0) List$compact(list, sizeof(item_type));                                           \
        item_type *i_ptr = (item_type *)(list->data + list->stride * i_off);                                           \
        item_type *j_ptr = (item_type *)(list->data + list->stride * j_off);                                           \
        item_type tmp = *i_ptr;                                                                                        \
        *i_ptr = *j_ptr;                                                                                               \
        *j_ptr = tmp;                                                                                                  \
        (void)0;                                                                                                       \
    })
// Guard for `for &x in xs` reference iteration (see compile_for_reference_loop
// in compile/loops.c): while raw element pointers into the buffer are live, the
// list must not be resized (buffer/length/stride would change under the
// pointers) or copied (a snapshot would alias a buffer we write without
// copy-on-write checks). Emitted once per iteration AND once after the loop
// (so a violation in the final iteration is still caught). The fast path is a
// single predicted-not-taken branch; message selection happens only in the
// cold failure block. This macro lives here, next to List$compact and the
// List_lvalue copy-on-write machinery, so changes to list internals update the
// borrow protocol in the same file.
#define List_ref_iter_guard(list_ptr, data0, n0, stride0, filename, start, end)                                        \
    do {                                                                                                               \
        const List_t *_l = (list_ptr);                                                                                 \
        if (unlikely(_l->data != (data0) || (int64_t)_l->length != (n0) || (int64_t)_l->stride != (stride0)            \
                     || _l->data_refcount > 0)) {                                                                      \
            if (_l->data_refcount > 0)                                                                                 \
                fail_source(filename, start, end,                                                                      \
                            Text("A copy of the list was made while a 'for &' loop was updating it\n"));               \
            fail_source(filename, start, end,                                                                          \
                        Text("The list was resized while a 'for &' loop was iterating over it\n"));                    \
        }                                                                                                              \
    } while (0)
#define is_atomic(x)                                                                                                   \
    _Generic(x,                                                                                                        \
        bool: true,                                                                                                    \
        int8_t: true,                                                                                                  \
        int16_t: true,                                                                                                 \
        int32_t: true,                                                                                                 \
        int64_t: true,                                                                                                 \
        float: true,                                                                                                   \
        double: true,                                                                                                  \
        default: false)
#define TypedList(t, ...)                                                                                              \
    ({                                                                                                                 \
        t items[] = {__VA_ARGS__};                                                                                     \
        (List_t){.length = sizeof(items) / sizeof(items[0]),                                                           \
                 .stride = (int64_t)&items[1] - (int64_t)&items[0],                                                    \
                 .data = sizeof(items) == 0 ? &_EMPTY_LIST_SENTINEL                                                    \
                                            : memcpy(GC_MALLOC(sizeof(items)), items, sizeof(items)),                  \
                 .atomic = 0,                                                                                          \
                 .data_refcount = 0};                                                                                  \
    })
#define TypedListN(t, N, ...)                                                                                          \
    ({                                                                                                                 \
        t items[N] = {__VA_ARGS__};                                                                                    \
        (List_t){.length = N,                                                                                          \
                 .stride = (int64_t)&items[1] - (int64_t)&items[0],                                                    \
                 .data = N == 0 ? &_EMPTY_LIST_SENTINEL : memcpy(GC_MALLOC(sizeof(items)), items, sizeof(items)),      \
                 .atomic = 0,                                                                                          \
                 .data_refcount = 0};                                                                                  \
    })
#define List(x, ...)                                                                                                   \
    ({                                                                                                                 \
        __typeof(x) items[] = {x, __VA_ARGS__};                                                                        \
        (List_t){.length = sizeof(items) / sizeof(items[0]),                                                           \
                 .stride = (int64_t)&items[1] - (int64_t)&items[0],                                                    \
                 .data = memcpy(is_atomic(x) ? GC_MALLOC_ATOMIC(sizeof(items)) : GC_MALLOC(sizeof(items)), items,      \
                                sizeof(items)),                                                                        \
                 .atomic = is_atomic(x),                                                                               \
                 .data_refcount = 0};                                                                                  \
    })
// List refcounts use a saturating add, where once it's at the max value, it stays there.
#define LIST_INCREF(list) (list).data_refcount += ((list).data_refcount < LIST_MAX_DATA_REFCOUNT)
#define LIST_DECREF(list) (list).data_refcount -= ((list).data_refcount < LIST_MAX_DATA_REFCOUNT)
#define LIST_COPY(list)                                                                                                \
    ({                                                                                                                 \
        LIST_INCREF(list);                                                                                             \
        list;                                                                                                          \
    })

#define List$insert_value(list, item_expr, index, padded_item_size)                                                    \
    List$insert(list, (__typeof(item_expr)[1]){item_expr}, index, padded_item_size)
// Append one item to the end of a list, inlining the common case (there is
// spare capacity, the list isn't an aliased/CoW snapshot, and the stride
// already matches): a bounds-free store plus two counter bumps, with no
// function call and no Int_t index to convert. Falls back to List$insert for
// the growth / resize / copy-on-write cases. Used to build list comprehensions
// (which append per element); `padded_item_size` is a compile-time sizeof, so
// the memcpy_fixed folds to a single store.
#define List$push_value(list, item_expr, padded_item_size)                                                             \
    ({                                                                                                                 \
        List_t *_push_l = (list);                                                                                      \
        __typeof(item_expr) _push_it = (item_expr);                                                                    \
        if (likely(_push_l->free > 0 && _push_l->data_refcount == 0                                                    \
                   && (int64_t)_push_l->stride == (int64_t)(padded_item_size))) {                                      \
            int64_t _push_n = (int64_t)_push_l->length;                                                                \
            memcpy_fixed((void *)_push_l->data + _push_n * (int64_t)(padded_item_size), &_push_it, padded_item_size);  \
            _push_l->length = (uint64_t)(_push_n + 1);                                                                 \
            _push_l->free -= 1;                                                                                        \
        } else {                                                                                                       \
            List$insert(_push_l, &_push_it, I_small(0), padded_item_size);                                             \
        }                                                                                                              \
        (void)0;                                                                                                       \
    })
// Allocate a list with `capacity` slots of spare room (length 0), so that up
// to `capacity` List$push_value appends hit the inlined store path with no
// resize. `zero_item` is any value of the item type (used only for its type,
// to pick atomic vs. scanned allocation and the element size).
#define List$with_capacity(capacity, zero_item)                                                                        \
    ({                                                                                                                 \
        int64_t _cap = (capacity);                                                                                     \
        int64_t _isz = (int64_t)sizeof(zero_item);                                                                     \
        _cap > 0 ? (List_t){.data = is_atomic(zero_item) ? GC_MALLOC_ATOMIC((size_t)(_cap * _isz))                     \
                                                         : GC_MALLOC((size_t)(_cap * _isz)),                           \
                            .length = 0, .free = _cap, .stride = _isz, .atomic = is_atomic(zero_item),                 \
                            .data_refcount = 0}                                                                        \
                 : (is_atomic(zero_item) ? EMPTY_ATOMIC_LIST : EMPTY_LIST);                                            \
    })
void List$insert(List_t *list, const void *item, Int_t index, int64_t padded_item_size);
void List$insert_all(List_t *list, List_t to_insert, Int_t index, int64_t padded_item_size);
void List$remove_at(List_t *list, Int_t index, Int_t count, int64_t padded_item_size);
void List$remove_item(List_t *list, void *item, Int_t max_removals, const TypeInfo_t *type);
#define List$remove_item_value(list, item_expr, max, type)                                                             \
    List$remove_item(list, (__typeof(item_expr)[1]){item_expr}, max, type)

#define List$pop(list_expr, index_expr, item_type, nonnone_var, nonnone_expr, none_expr)                               \
    ({                                                                                                                 \
        List_t *list = list_expr;                                                                                      \
        Int_t index = index_expr;                                                                                      \
        int64_t index64 = Int64$from_int(index, false);                                                                \
        int64_t off = index64 + (index64 < 0) * (list->length + 1) - 1;                                                \
        (off >= 0 && off < list->length) ? ({                                                                          \
            item_type nonnone_var = *(item_type *)(list->data + off * list->stride);                                   \
            List$remove_at(list, index, I_small(1), sizeof(item_type));                                                \
            nonnone_expr;                                                                                              \
        })                                                                                                             \
                                         : none_expr;                                                                  \
    })

PUREFUNC OptionalInt_t List$find(List_t list, void *item, const TypeInfo_t *type);
#define List$find_value(list, item_expr, type)                                                                         \
    ({                                                                                                                 \
        __typeof(item_expr) item = item_expr;                                                                          \
        List$find(list, &item, type);                                                                                  \
    })
OptionalInt_t List$first(List_t list, Closure_t predicate);
void List$sort(List_t *list, Closure_t comparison, int64_t padded_item_size);
List_t List$sorted(List_t list, Closure_t comparison, int64_t padded_item_size);
void List$shuffle(List_t *list, OptionalClosure_t random_int64, int64_t padded_item_size);
List_t List$shuffled(List_t list, OptionalClosure_t random_int64, int64_t padded_item_size);
void *List$random(List_t list, OptionalClosure_t random_int64);
#define List$random_value(list, random_fn, t, nonnone_var, nonnone_expr, none_expr)                                    \
    ({                                                                                                                 \
        List_t _list_expr = list;                                                                                      \
        (_list_expr.length == 0) ? none_expr : ({                                                                      \
            t nonnone_var = *(t *)List$random(_list_expr, random_fn);                                                  \
            nonnone_expr;                                                                                              \
        });                                                                                                            \
    })
List_t List$sample(List_t list, Int_t n, List_t weights, Closure_t random_num, int64_t padded_item_size);
Table_t List$counts(List_t list, const TypeInfo_t *type);
void List$clear(List_t *list);
void List$compact(List_t *list, int64_t padded_item_size);
PUREFUNC bool List$has(List_t list, void *item, const TypeInfo_t *type);
#define List$has_value(list, item_expr, type)                                                                          \
    ({                                                                                                                 \
        __typeof(item_expr) item = item_expr;                                                                          \
        List$has(list, &item, type);                                                                                   \
    })
PUREFUNC List_t List$from(List_t list, Int_t first);
PUREFUNC List_t List$to(List_t list, Int_t last);
PUREFUNC List_t List$by(List_t list, Int_t stride, int64_t padded_item_size);
PUREFUNC List_t List$slice(List_t list, Int_t int_first, Int_t int_last);
PUREFUNC List_t List$reversed(List_t list, int64_t padded_item_size);
Closure_t List$pairs(List_t list, int64_t padded_item_size);
List_t List$concat(List_t x, List_t y, int64_t padded_item_size);
PUREFUNC uint64_t List$hash(const void *list, const TypeInfo_t *type);
PUREFUNC int32_t List$compare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool List$equal(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool List$is_none(const void *obj, const TypeInfo_t *);
Text_t List$as_text(const void *list, bool colorize, const TypeInfo_t *type);
void List$heapify(List_t *heap, Closure_t comparison, int64_t padded_item_size);
void List$heap_push(List_t *heap, const void *item, Closure_t comparison, int64_t padded_item_size);
#define List$heap_push_value(heap, _value, comparison, padded_item_size)                                               \
    ({                                                                                                                 \
        __typeof(_value) value = _value;                                                                               \
        List$heap_push(heap, &value, comparison, padded_item_size);                                                    \
    })
void List$heap_pop(List_t *heap, Closure_t comparison, int64_t padded_item_size);
#define List$heap_pop_value(heap, comparison, type, nonnone_var, nonnone_expr, none_expr)                              \
    ({                                                                                                                 \
        List_t *_heap = heap;                                                                                          \
        (_heap->length > 0) ? ({                                                                                       \
            type nonnone_var = *(type *)_heap->data;                                                                   \
            List$heap_pop(_heap, comparison, sizeof(type));                                                            \
            nonnone_expr;                                                                                              \
        })                                                                                                             \
                            : none_expr;                                                                               \
    })
OptionalInt_t List$binary_search(List_t list, Closure_t comparison);
void List$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type);
void List$deserialize(FILE *in, void *obj, List_t *pointers, const TypeInfo_t *type);

#define List$metamethods                                                                                               \
    {                                                                                                                  \
        .as_text = List$as_text,                                                                                       \
        .compare = List$compare,                                                                                       \
        .equal = List$equal,                                                                                           \
        .hash = List$hash,                                                                                             \
        .is_none = List$is_none,                                                                                       \
        .serialize = List$serialize,                                                                                   \
        .deserialize = List$deserialize,                                                                               \
    }

#define List$info(item_info)                                                                                           \
    &((TypeInfo_t){.size = sizeof(List_t),                                                                             \
                   .align = __alignof__(List_t),                                                                       \
                   .tag = ListInfo,                                                                                    \
                   .ListInfo.item = item_info,                                                                         \
                   .metamethods = List$metamethods})
