// Functions that operate on lists

#include <gc.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/param.h>

#include "integers.h"
#include "lists.h"
#include "math.h"
#include "metamethods.h"
#include "optionals.h"
#include "tables.h"
#include "text.h"
#include "util.h"

// Use inline version of siphash code:
#include "siphash-internals.h"

PUREFUNC static INLINE int64_t get_padded_item_size(const TypeInfo_t *info) {
    int64_t size = info->ListInfo.item->size;
    if (info->ListInfo.item->align > 1 && size % info->ListInfo.item->align) errx(1, "Item size is not padded!");
    return size;
}

// Replace the list's .data pointer with a new pointer to a copy of the
// data that is compacted and has a stride of exactly `padded_item_size`
public
void List$compact(List_t *list, int64_t padded_item_size) {
    void *copy = NULL;
    if (list->length > 0) {
        copy = list->atomic ? GC_MALLOC_ATOMIC((size_t)list->length * (size_t)padded_item_size)
                            : GC_MALLOC((size_t)list->length * (size_t)padded_item_size);
        if ((int64_t)list->stride == padded_item_size) {
            memcpy(copy, list->data, (size_t)list->length * (size_t)padded_item_size);
        } else {
            for (int64_t i = 0; i < list->length; i++)
                memcpy(copy + i * padded_item_size, list->data + list->stride * i, (size_t)padded_item_size);
        }
    }
    *list = (List_t){
        .data = copy,
        .length = list->length,
        .stride = padded_item_size,
        .atomic = list->atomic,
    };
}

public
void List$insert(List_t *list, const void *item, Int_t int_index, int64_t padded_item_size) {
    int64_t index = Int64$from_int(int_index, false);
    if (index <= 0) index = list->length + index + 1;

    if (index < 1) index = 1;
    else if (index > (int64_t)list->length + 1)
        fail("Invalid insertion index ", index, " for a list with length ", (int64_t)list->length);

    if (!list->data) {
        list->free = 4;
        list->data = list->atomic ? GC_MALLOC_ATOMIC((size_t)list->free * (size_t)padded_item_size)
                                  : GC_MALLOC((size_t)list->free * (size_t)padded_item_size);
        list->stride = padded_item_size;
    } else if (list->free < 1 || list->data_refcount != 0 || (int64_t)list->stride != padded_item_size) {
        // Resize policy: +50% growth (clamped between 8 and LIST_MAX_FREE_ENTRIES)
        list->free = MIN(LIST_MAX_FREE_ENTRIES, MAX(8, list->length) / 2);
        void *copy = list->atomic ? GC_MALLOC_ATOMIC((size_t)(list->length + list->free) * (size_t)padded_item_size)
                                  : GC_MALLOC((size_t)(list->length + list->free) * (size_t)padded_item_size);
        for (int64_t i = 0; i < index - 1; i++)
            memcpy(copy + i * padded_item_size, list->data + list->stride * i, (size_t)padded_item_size);
        for (int64_t i = index - 1; i < (int64_t)list->length; i++)
            memcpy(copy + (i + 1) * padded_item_size, list->data + list->stride * i, (size_t)padded_item_size);
        list->data = copy;
        list->data_refcount = 0;
        list->stride = padded_item_size;
    } else {
        if (index != list->length + 1) {
            assert(list->length >= index);
            size_t size = (size_t)((list->length - index + 1) * padded_item_size);
            assert(size < SIZE_MAX);
            memmove(list->data + index * padded_item_size, list->data + (index - 1) * padded_item_size, size);
        }
    }
    assert(list->free > 0);
    --list->free;
    ++list->length;
    memcpy((void *)list->data + (index - 1) * padded_item_size, item, (size_t)padded_item_size);
}

public
void List$insert_all(List_t *list, List_t to_insert, Int_t int_index, int64_t padded_item_size) {
    int64_t index = Int64$from_int(int_index, false);
    if (to_insert.length == 0) return;

    if (!list->data) {
        *list = to_insert;
        LIST_INCREF(*list);
        return;
    }

    if (index < 1) index = list->length + index + 1;

    if (index < 1) index = 1;
    else if (index > (int64_t)list->length + 1)
        fail("Invalid insertion index ", index, " for a list with length ", (int64_t)list->length);

    if ((int64_t)list->free >= (int64_t)to_insert.length // Adequate free space
        && list->data_refcount == 0                      // Not aliased memory
        && (int64_t)list->stride == padded_item_size) {  // Contiguous list
        // If we can fit this within the list's preallocated free space, do that:
        list->free -= to_insert.length;
        list->length += to_insert.length;
        if (index != list->length + 1)
            memmove((void *)list->data + index * padded_item_size, list->data + (index - 1) * padded_item_size,
                    (size_t)((list->length - index + to_insert.length - 1) * padded_item_size));
        for (int64_t i = 0; i < to_insert.length; i++)
            memcpy((void *)list->data + (index - 1 + i) * padded_item_size, to_insert.data + i * to_insert.stride,
                   (size_t)padded_item_size);
    } else {
        // Otherwise, allocate a new chunk of memory for the list and populate it:
        int64_t new_len = list->length + to_insert.length;
        list->free = MIN(LIST_MAX_FREE_ENTRIES, MAX(8, new_len / 4));
        void *data = list->atomic ? GC_MALLOC_ATOMIC((size_t)((new_len + list->free) * padded_item_size))
                                  : GC_MALLOC((size_t)((new_len + list->free) * padded_item_size));
        void *p = data;

        // Copy first chunk of `list` if needed:
        if (index > 1) {
            if (list->stride == padded_item_size) {
                memcpy(p, list->data, (size_t)((index - 1) * padded_item_size));
                p += (index - 1) * padded_item_size;
            } else {
                for (int64_t i = 0; i < index - 1; i++) {
                    memcpy(p, list->data + list->stride * i, (size_t)padded_item_size);
                    p += padded_item_size;
                }
            }
        }

        // Copy `to_insert`
        if (to_insert.stride == padded_item_size) {
            memcpy(p, to_insert.data, (size_t)(to_insert.length * padded_item_size));
            p += to_insert.length * padded_item_size;
        } else {
            for (int64_t i = 0; i < index - 1; i++) {
                memcpy(p, to_insert.data + to_insert.stride * i, (size_t)padded_item_size);
                p += padded_item_size;
            }
        }

        // Copy last chunk of `list` if needed:
        if (index < list->length + 1) {
            if (list->stride == padded_item_size) {
                memcpy(p, list->data + padded_item_size * (index - 1),
                       (size_t)((list->length - index + 1) * padded_item_size));
                p += (list->length - index + 1) * padded_item_size;
            } else {
                for (int64_t i = index - 1; i < list->length - 1; i++) {
                    memcpy(p, list->data + list->stride * i, (size_t)padded_item_size);
                    p += padded_item_size;
                }
            }
        }
        list->length = new_len;
        list->stride = padded_item_size;
        list->data = data;
        list->data_refcount = 0;
    }
}

public
void List$remove_at(List_t *list, Int_t int_index, Int_t int_count, int64_t padded_item_size) {
    int64_t index = Int64$from_int(int_index, false);
    if (index < 1) index = list->length + index + 1;

    int64_t count = Int64$from_int(int_count, false);
    if (index < 1 || index > (int64_t)list->length || count < 1) return;

    if (count > list->length - index + 1) count = (list->length - index) + 1;

    if (index == 1) {
        list->data += list->stride * count;
    } else if (index + count > list->length) {
        list->free += count;
    } else if (list->data_refcount != 0 || (int64_t)list->stride != padded_item_size) {
        void *copy = list->atomic ? GC_MALLOC_ATOMIC((size_t)((list->length - 1) * padded_item_size))
                                  : GC_MALLOC((size_t)((list->length - 1) * padded_item_size));
        for (int64_t src = 1, dest = 1; src <= (int64_t)list->length; src++) {
            if (src < index || src >= index + count) {
                memcpy(copy + (dest - 1) * padded_item_size, list->data + list->stride * (src - 1),
                       (size_t)padded_item_size);
                ++dest;
            }
        }
        list->data = copy;
        list->free = 0;
        list->data_refcount = 0;
    } else {
        memmove((void *)list->data + (index - 1) * padded_item_size,
                list->data + (index - 1 + count) * padded_item_size,
                (size_t)((list->length - index + count - 1) * padded_item_size));
        list->free += count;
    }
    list->length -= count;
    if (list->length == 0) list->data = NULL;
}

public
void List$remove_item(List_t *list, void *item, Int_t max_removals, const TypeInfo_t *type) {
    int64_t padded_item_size = get_padded_item_size(type);
    const Int_t ZERO = (Int_t){.small = (0 << 2) | 1};
    const Int_t ONE = (Int_t){.small = (1 << 2) | 1};
    const TypeInfo_t *item_type = type->ListInfo.item;
    for (int64_t i = 0; i < list->length;) {
        if (max_removals.small == ZERO.small) // zero
            break;

        if (generic_equal(item, list->data + i * list->stride, item_type)) {
            List$remove_at(list, I(i + 1), ONE, padded_item_size);
            max_removals = Int$minus(max_removals, ONE);
        } else {
            i++;
        }
    }
}

public
OptionalInt_t List$find(List_t list, void *item, const TypeInfo_t *type) {
    const TypeInfo_t *item_type = type->ListInfo.item;
    for (int64_t i = 0; i < list.length; i++) {
        if (generic_equal(item, list.data + i * list.stride, item_type)) return I(i + 1);
    }
    return NONE_INT;
}

public
OptionalInt_t List$first(List_t list, Closure_t predicate) {
    bool (*is_good)(void *, void *) = (void *)predicate.fn;
    for (int64_t i = 0; i < list.length; i++) {
        if (is_good(list.data + i * list.stride, predicate.userdata)) return I(i + 1);
    }
    return NONE_INT;
}

static Closure_t _sort_comparison = {.fn = NULL};

int _compare_closure(const void *a, const void *b) {
    typedef int (*comparison_t)(const void *, const void *, void *);
    return ((comparison_t)_sort_comparison.fn)(a, b, _sort_comparison.userdata);
}

public
void List$sort(List_t *list, Closure_t comparison, int64_t padded_item_size) {
    if (list->data_refcount != 0 || (int64_t)list->stride != padded_item_size) List$compact(list, padded_item_size);

    _sort_comparison = comparison;
    qsort(list->data, (size_t)list->length, (size_t)padded_item_size, _compare_closure);
}

public
List_t List$sorted(List_t list, Closure_t comparison, int64_t padded_item_size) {
    List$compact(&list, padded_item_size);
    _sort_comparison = comparison;
    qsort(list.data, (size_t)list.length, (size_t)padded_item_size, _compare_closure);
    return list;
}

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
#include <stdlib.h>
static ssize_t getrandom(void *buf, size_t buflen, unsigned int flags) {
    (void)flags;
    arc4random_buf(buf, buflen);
    return buflen;
}
#elif defined(__linux__)
// Use getrandom()
#include <sys/random.h>
#else
#error "Unsupported platform for secure random number generation"
#endif

static int64_t _default_random_int64(int64_t min, int64_t max, void *userdata) {
    (void)userdata;
    if (min > max) fail("Random minimum value (", min, ") is larger than the maximum value (", max, ")");
    if (min == max) return min;
    uint64_t range = (uint64_t)max - (uint64_t)min + 1;
    uint64_t min_r = -range % range;
    uint64_t r;
    for (;;) {
        assert(getrandom(&r, sizeof(r), 0) == sizeof(r));
        if (r >= min_r) break;
    }
    return (int64_t)((uint64_t)min + (r % range));
}

public
void List$shuffle(List_t *list, OptionalClosure_t random_int64, int64_t padded_item_size) {
    if (list->data_refcount != 0 || (int64_t)list->stride != padded_item_size) List$compact(list, padded_item_size);

    typedef int64_t (*rng_fn_t)(int64_t, int64_t, void *);
    rng_fn_t rng_fn = random_int64.fn ? (rng_fn_t)random_int64.fn : _default_random_int64;
    char tmp[padded_item_size];
    for (int64_t i = list->length - 1; i > 1; i--) {
        int64_t j = rng_fn(0, i, random_int64.userdata);
        if unlikely (j < 0 || j > list->length - 1)
            fail("The provided random number function returned an invalid value: ", j, " (not between 0 and ", i, ")");
        memcpy(tmp, list->data + i * padded_item_size, (size_t)padded_item_size);
        memcpy((void *)list->data + i * padded_item_size, list->data + j * padded_item_size, (size_t)padded_item_size);
        memcpy((void *)list->data + j * padded_item_size, tmp, (size_t)padded_item_size);
    }
}

public
List_t List$shuffled(List_t list, Closure_t random_int64, int64_t padded_item_size) {
    List$compact(&list, padded_item_size);
    List$shuffle(&list, random_int64, padded_item_size);
    return list;
}

public
void *List$random(List_t list, OptionalClosure_t random_int64) {
    if (list.length == 0) return NULL; // fail("Cannot get a random item from an empty list!");

    typedef int64_t (*rng_fn_t)(int64_t, int64_t, void *);
    rng_fn_t rng_fn = random_int64.fn ? (rng_fn_t)random_int64.fn : _default_random_int64;
    int64_t index = rng_fn(0, list.length - 1, random_int64.userdata);
    if unlikely (index < 0 || index > list.length - 1)
        fail("The provided random number function returned an invalid value: ", index, " (not between 0 and ",
             (int64_t)list.length, ")");
    return list.data + list.stride * index;
}

public
Table_t List$counts(List_t list, const TypeInfo_t *type) {
    Table_t counts = {};
    const TypeInfo_t count_type = *Table$info(type->ListInfo.item, &Int$info);
    for (int64_t i = 0; i < list.length; i++) {
        void *key = list.data + i * list.stride;
        int64_t *count = Table$get(counts, key, &count_type);
        int64_t val = count ? *count + 1 : 1;
        Table$set(&counts, key, &val, &count_type);
    }
    return counts;
}

static double _default_random_num(void *userdata) {
    (void)userdata;
    union {
        Num_t num;
        uint64_t bits;
    } r = {.bits = 0}, one = {.num = 1.0};
    assert(getrandom((uint8_t *)&r, sizeof(r), 0) == sizeof(r));

    // Set r.num to 1.<random-bits>
    r.bits &= ~(0xFFFULL << 52);
    r.bits |= (one.bits & (0xFFFULL << 52));
    return r.num - 1.0;
}

public
List_t List$sample(List_t list, Int_t int_n, List_t weights, OptionalClosure_t random_num, int64_t padded_item_size) {
    int64_t n = Int64$from_int(int_n, false);
    if (n < 0) fail("Cannot select a negative number of values");

    if (n == 0) return (List_t){};

    if (list.length == 0) fail("There are no elements in this list!");

    if (weights.length != list.length)
        fail("List has ", (int64_t)list.length, " elements, but there are ", (int64_t)weights.length, " weights given");

    double total = 0.0;
    for (int64_t i = 0; i < weights.length && i < list.length; i++) {
        double weight = *(double *)(weights.data + weights.stride * i);
        if (isinf(weight)) fail("Infinite weight!");
        else if (isnan(weight)) fail("NaN weight!");
        else if (weight < 0.0) fail("Negative weight!");
        else total += weight;
    }

    if (isinf(total)) fail("Sample weights have overflowed to infinity");

    if (total == 0.0) fail("None of the given weights are nonzero");

    double inverse_average = (double)list.length / total;

    struct {
        int64_t alias;
        double odds;
    } aliases[list.length];

    for (int64_t i = 0; i < list.length; i++) {
        double weight = i >= weights.length ? 0.0 : *(double *)(weights.data + weights.stride * i);
        aliases[i].odds = weight * inverse_average;
        aliases[i].alias = -1;
    }

    int64_t small = 0;
    for (int64_t big = 0; big < list.length; big++) {
        while (aliases[big].odds >= 1.0) {
            while (small < list.length && (aliases[small].odds >= 1.0 || aliases[small].alias != -1))
                ++small;

            if (small >= list.length) {
                aliases[big].odds = 1.0;
                aliases[big].alias = big;
                break;
            }

            aliases[small].alias = big;
            aliases[big].odds = (aliases[small].odds + aliases[big].odds) - 1.0;
        }
        if (big < small) small = big;
    }

    for (int64_t i = small; i < list.length; i++)
        if (aliases[i].alias == -1) aliases[i].alias = i;

    typedef double (*rng_fn_t)(void *);
    rng_fn_t rng_fn = random_num.fn ? (rng_fn_t)random_num.fn : _default_random_num;

    List_t selected = {.data = list.atomic ? GC_MALLOC_ATOMIC((size_t)(n * padded_item_size))
                                           : GC_MALLOC((size_t)(n * padded_item_size)),
                       .length = n,
                       .stride = padded_item_size,
                       .atomic = list.atomic};
    for (int64_t i = 0; i < n; i++) {
        double r = rng_fn(random_num.userdata);
        if unlikely (r < 0.0 || r >= 1.0)
            fail("The random number function returned a value not between 0.0 (inclusive) and 1.0 (exclusive): ", r);
        r *= (double)list.length;
        int64_t index = (int64_t)r;
        assert(index >= 0 && index < list.length);
        if ((r - (double)index) > aliases[index].odds) index = aliases[index].alias;
        memcpy(selected.data + i * selected.stride, list.data + index * list.stride, (size_t)padded_item_size);
    }
    return selected;
}

public
List_t List$from(List_t list, Int_t first) { return List$slice(list, first, I_small(-1)); }

public
List_t List$to(List_t list, Int_t last) { return List$slice(list, I_small(1), last); }

public
List_t List$by(List_t list, Int_t int_stride, int64_t padded_item_size) {
    int64_t stride = Int64$from_int(int_stride, false);
    // In the unlikely event that the stride value would be too large to fit in
    // a 15-bit integer, fall back to creating a copy of the list:
    if (unlikely(list.stride * stride < LIST_MIN_STRIDE || list.stride * stride > LIST_MAX_STRIDE)) {
        void *copy = NULL;
        int64_t len = (stride < 0 ? list.length / -stride : list.length / stride) + ((list.length % stride) != 0);
        if (len > 0) {
            copy = list.atomic ? GC_MALLOC_ATOMIC((size_t)(len * padded_item_size))
                               : GC_MALLOC((size_t)(len * padded_item_size));
            void *start = (stride < 0 ? list.data + (list.stride * (list.length - 1)) : list.data);
            for (int64_t i = 0; i < len; i++)
                memcpy(copy + i * padded_item_size, start + list.stride * stride * i, (size_t)padded_item_size);
        }
        return (List_t){
            .data = copy,
            .length = len,
            .stride = padded_item_size,
            .atomic = list.atomic,
        };
    }

    if (stride == 0) return (List_t){.atomic = list.atomic};

    return (List_t){
        .atomic = list.atomic,
        .data = (stride < 0 ? list.data + (list.stride * (list.length - 1)) : list.data),
        .length = (stride < 0 ? list.length / -stride : list.length / stride) + ((list.length % stride) != 0),
        .stride = list.stride * stride,
        .data_refcount = list.data_refcount,
    };
}

public
List_t List$slice(List_t list, Int_t int_first, Int_t int_last)

{
    int64_t first = Int64$from_int(int_first, false);
    if (first < 0) first = list.length + first + 1;

    int64_t last = Int64$from_int(int_last, false);
    if (last < 0) last = list.length + last + 1;

    if (last > list.length) last = list.length;

    if (first < 1 || first > list.length || last == 0) return (List_t){.atomic = list.atomic};

    return (List_t){
        .atomic = list.atomic,
        .data = list.data + list.stride * (first - 1),
        .length = last - first + 1,
        .stride = list.stride,
        .data_refcount = list.data_refcount,
    };
}

public
List_t List$reversed(List_t list, int64_t padded_item_size) {
    // Just in case negating the stride gives a value that doesn't fit into a
    // 15-bit integer, fall back to List$by()'s more general method of copying
    // the list. This should only happen if list.stride is MIN_STRIDE to
    // begin with (very unlikely).
    if (unlikely(-list.stride < LIST_MIN_STRIDE || -list.stride > LIST_MAX_STRIDE))
        return List$by(list, I(-1), padded_item_size);

    List_t reversed = list;
    reversed.stride = -list.stride;
    reversed.data = list.data + (list.length - 1) * list.stride;
    return reversed;
}

public
List_t List$concat(List_t x, List_t y, int64_t padded_item_size) {
    void *data = x.atomic ? GC_MALLOC_ATOMIC((size_t)(padded_item_size * (x.length + y.length)))
                          : GC_MALLOC((size_t)(padded_item_size * (x.length + y.length)));
    if (x.stride == padded_item_size) {
        memcpy(data, x.data, (size_t)(padded_item_size * x.length));
    } else {
        for (int64_t i = 0; i < x.length; i++)
            memcpy(data + i * padded_item_size, x.data + i * padded_item_size, (size_t)padded_item_size);
    }

    void *dest = data + padded_item_size * x.length;
    if (y.stride == padded_item_size) {
        memcpy(dest, y.data, (size_t)(padded_item_size * y.length));
    } else {
        for (int64_t i = 0; i < y.length; i++)
            memcpy(dest + i * padded_item_size, y.data + i * y.stride, (size_t)padded_item_size);
    }

    return (List_t){
        .data = data,
        .length = x.length + y.length,
        .stride = padded_item_size,
        .atomic = x.atomic,
    };
}

public
bool List$has(List_t list, void *item, const TypeInfo_t *type) {
    const TypeInfo_t *item_type = type->ListInfo.item;
    for (int64_t i = 0; i < list.length; i++) {
        if (generic_equal(list.data + i * list.stride, item, item_type)) return true;
    }
    return false;
}

public
void List$clear(List_t *list) { *list = (List_t){.data = 0, .length = 0}; }

public
int32_t List$compare(const void *vx, const void *vy, const TypeInfo_t *type) {
    const List_t *x = (List_t *)vx, *y = (List_t *)vy;
    // Early out for lists with the same data, e.g. two copies of the same list:
    if (x->data == y->data && x->stride == y->stride) return (x->length > y->length) - (x->length < y->length);

    const TypeInfo_t *item = type->ListInfo.item;
    if (item->tag == PointerInfo || !item->metamethods.compare) { // data comparison
        int64_t item_padded_size = type->ListInfo.item->size;
        if (type->ListInfo.item->align > 1 && item_padded_size % type->ListInfo.item->align)
            errx(1, "Item size is not padded!");

        if ((int64_t)x->stride == item_padded_size && (int64_t)y->stride == item_padded_size
            && item->size == item_padded_size) {
            int32_t cmp = (int32_t)memcmp(x->data, y->data, (size_t)(MIN(x->length, y->length) * item_padded_size));
            if (cmp != 0) return cmp;
        } else {
            for (int32_t i = 0, len = MIN(x->length, y->length); i < len; i++) {
                int32_t cmp = (int32_t)memcmp(x->data + x->stride * i, y->data + y->stride * i, (size_t)(item->size));
                if (cmp != 0) return cmp;
            }
        }
    } else {
        for (int32_t i = 0, len = MIN(x->length, y->length); i < len; i++) {
            int32_t cmp = generic_compare(x->data + x->stride * i, y->data + y->stride * i, item);
            if (cmp != 0) return cmp;
        }
    }
    return (x->length > y->length) - (x->length < y->length);
}

public
bool List$equal(const void *x, const void *y, const TypeInfo_t *type) {
    return x == y || (((List_t *)x)->length == ((List_t *)y)->length && List$compare(x, y, type) == 0);
}

public
Text_t List$as_text(const void *obj, bool colorize, const TypeInfo_t *type) {
    List_t *list = (List_t *)obj;
    if (!list) return Text$concat(Text("["), generic_as_text(NULL, false, type->ListInfo.item), Text("]"));

    const TypeInfo_t *item_type = type->ListInfo.item;
    Text_t text = Text("[");
    for (int64_t i = 0; i < list->length; i++) {
        if (i > 0) text = Text$concat(text, Text(", "));
        Text_t item_text = generic_as_text(list->data + i * list->stride, colorize, item_type);
        text = Text$concat(text, item_text);
    }
    text = Text$concat(text, Text("]"));
    return text;
}

public
uint64_t List$hash(const void *obj, const TypeInfo_t *type) {
    const List_t *list = (List_t *)obj;
    const TypeInfo_t *item = type->ListInfo.item;
    siphash sh;
    siphashinit(&sh, sizeof(uint64_t[list->length]));
    if (item->tag == PointerInfo || (!item->metamethods.hash && item->size == sizeof(void *))) { // Raw data hash
        for (int64_t i = 0; i < list->length; i++)
            siphashadd64bits(&sh, (uint64_t)(list->data + i * list->stride));
    } else {
        for (int64_t i = 0; i < list->length; i++) {
            uint64_t item_hash = generic_hash(list->data + i * list->stride, item);
            siphashadd64bits(&sh, item_hash);
        }
    }
    return siphashfinish_last_part(&sh, 0);
}

static void siftdown(List_t *heap, int64_t startpos, int64_t pos, Closure_t comparison, int64_t padded_item_size) {
    assert(pos > 0 && pos < heap->length);
    char newitem[padded_item_size];
    memcpy(newitem, heap->data + heap->stride * pos, (size_t)(padded_item_size));
    while (pos > startpos) {
        int64_t parentpos = (pos - 1) >> 1;
        typedef int32_t (*cmp_fn_t)(void *, void *, void *);
        int32_t cmp = ((cmp_fn_t)comparison.fn)(newitem, heap->data + heap->stride * parentpos, comparison.userdata);
        if (cmp >= 0) break;

        memcpy(heap->data + heap->stride * pos, heap->data + heap->stride * parentpos, (size_t)(padded_item_size));
        pos = parentpos;
    }
    memcpy(heap->data + heap->stride * pos, newitem, (size_t)(padded_item_size));
}

static void siftup(List_t *heap, int64_t pos, Closure_t comparison, int64_t padded_item_size) {
    int64_t endpos = heap->length;
    int64_t startpos = pos;
    assert(pos < endpos);

    char old_top[padded_item_size];
    memcpy(old_top, heap->data + heap->stride * pos, (size_t)(padded_item_size));
    // Bubble up the smallest leaf node
    int64_t limit = endpos >> 1;
    while (pos < limit) {
        int64_t childpos = 2 * pos + 1; // Smaller of the two child nodes
        if (childpos + 1 < endpos) {
            typedef int32_t (*cmp_fn_t)(void *, void *, void *);
            int32_t cmp = ((cmp_fn_t)comparison.fn)(heap->data + heap->stride * childpos,
                                                    heap->data + heap->stride * (childpos + 1), comparison.userdata);
            childpos += (cmp >= 0);
        }

        // Move the child node up:
        memcpy(heap->data + heap->stride * pos, heap->data + heap->stride * childpos, (size_t)(padded_item_size));
        pos = childpos;
    }
    memcpy(heap->data + heap->stride * pos, old_top, (size_t)(padded_item_size));
    // Shift the node's parents down:
    siftdown(heap, startpos, pos, comparison, padded_item_size);
}

public
void List$heap_push(List_t *heap, const void *item, Closure_t comparison, int64_t padded_item_size) {
    List$insert(heap, item, I(0), padded_item_size);

    if (heap->length > 1) {
        if (heap->data_refcount != 0) List$compact(heap, padded_item_size);
        siftdown(heap, 0, heap->length - 1, comparison, padded_item_size);
    }
}

public
void List$heap_pop(List_t *heap, Closure_t comparison, int64_t padded_item_size) {
    if (heap->length == 0) fail("Attempt to pop from an empty list");

    if (heap->length == 1) {
        *heap = (List_t){};
    } else if (heap->length == 2) {
        heap->data += heap->stride;
        --heap->length;
    } else {
        if (heap->data_refcount != 0) List$compact(heap, padded_item_size);
        memcpy(heap->data, heap->data + heap->stride * (heap->length - 1), (size_t)(padded_item_size));
        --heap->length;
        siftup(heap, 0, comparison, padded_item_size);
    }
}

public
void List$heapify(List_t *heap, Closure_t comparison, int64_t padded_item_size) {
    if (heap->data_refcount != 0) List$compact(heap, padded_item_size);

    // It's necessary to bump the refcount because the user's comparison
    // function could do stuff that modifies the heap's data.
    LIST_INCREF(*heap);
    int64_t i, n = heap->length;
    for (i = (n >> 1) - 1; i >= 0; i--)
        siftup(heap, i, comparison, padded_item_size);
    LIST_DECREF(*heap);
}

public
Int_t List$binary_search(List_t list, void *target, Closure_t comparison) {
    typedef int32_t (*cmp_fn_t)(void *, void *, void *);
    int64_t lo = 0, hi = list.length - 1;
    while (lo <= hi) {
        int64_t mid = (lo + hi) / 2;
        int32_t cmp = ((cmp_fn_t)comparison.fn)(list.data + list.stride * mid, target, comparison.userdata);
        if (cmp == 0) return I(mid + 1);
        else if (cmp < 0) lo = mid + 1;
        else if (cmp > 0) hi = mid - 1;
    }
    return I(lo + 1); // Return the index where the target would be inserted
}

public
PUREFUNC bool List$is_none(const void *obj, const TypeInfo_t *info) {
    (void)info;
    return ((List_t *)obj)->length < 0;
}

public
void List$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type) {
    List_t list = *(List_t *)obj;
    int64_t len = list.length;
    Int64$serialize(&len, out, pointers, &Int64$info);
    serialize_fn_t item_serialize = type->ListInfo.item->metamethods.serialize;
    if (item_serialize) {
        for (int64_t i = 0; i < len; i++)
            item_serialize(list.data + i * list.stride, out, pointers, type->ListInfo.item);
    } else if (list.stride == type->ListInfo.item->size) {
        fwrite(list.data, (size_t)type->ListInfo.item->size, (size_t)len, out);
    } else {
        for (int64_t i = 0; i < len; i++)
            fwrite(list.data + i * list.stride, (size_t)type->ListInfo.item->size, 1, out);
    }
}

public
void List$deserialize(FILE *in, void *obj, List_t *pointers, const TypeInfo_t *type) {
    int64_t len = -1;
    Int64$deserialize(in, &len, pointers, &Int64$info);
    int64_t padded_size = type->ListInfo.item->size;
    if (type->ListInfo.item->align > 0 && padded_size % type->ListInfo.item->align > 0)
        padded_size += type->ListInfo.item->align - (padded_size % type->ListInfo.item->align);
    List_t list = {
        .length = len,
        .data = GC_MALLOC((size_t)(len * padded_size)),
        .stride = padded_size,
    };
    deserialize_fn_t item_deserialize = type->ListInfo.item->metamethods.deserialize;
    if (item_deserialize) {
        for (int64_t i = 0; i < len; i++)
            item_deserialize(in, list.data + i * list.stride, pointers, type->ListInfo.item);
    } else if (list.stride == type->ListInfo.item->size) {
        if (fread(list.data, (size_t)type->ListInfo.item->size, (size_t)len, in) != (size_t)len)
            fail("Not enough data in stream to deserialize");
    } else {
        size_t item_size = (size_t)type->ListInfo.item->size;
        for (int64_t i = 0; i < len; i++) {
            if (fread(list.data + i * list.stride, item_size, 1, in) != 1)
                fail("Not enough data in stream to deserialize");
        }
    }
    *(List_t *)obj = list;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
