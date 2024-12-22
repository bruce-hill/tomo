// Functions that operate on arrays

#include <gc.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/param.h>

#include "arrays.h"
#include "integers.h"
#include "metamethods.h"
#include "optionals.h"
#include "rng.h"
#include "tables.h"
#include "text.h"
#include "util.h"

// Use inline version of siphash code:
#include "siphash.h"
#include "siphash-internals.h"

PUREFUNC static INLINE int64_t get_padded_item_size(const TypeInfo_t *info)
{
    int64_t size = info->ArrayInfo.item->size;
    if (info->ArrayInfo.item->align > 1 && size % info->ArrayInfo.item->align)
        errx(1, "Item size is not padded!");
    return size;
}

// Replace the array's .data pointer with a new pointer to a copy of the
// data that is compacted and has a stride of exactly `padded_item_size`
public void Array$compact(Array_t *arr, int64_t padded_item_size)
{
    void *copy = NULL;
    if (arr->length > 0) {
        copy = arr->atomic ? GC_MALLOC_ATOMIC((size_t)arr->length * (size_t)padded_item_size)
            : GC_MALLOC((size_t)arr->length * (size_t)padded_item_size);
        if ((int64_t)arr->stride == padded_item_size) {
            memcpy(copy, arr->data, (size_t)arr->length * (size_t)padded_item_size);
        } else {
            for (int64_t i = 0; i < arr->length; i++)
                memcpy(copy + i*padded_item_size, arr->data + arr->stride*i, (size_t)padded_item_size);
        }
    }
    *arr = (Array_t){
        .data=copy,
        .length=arr->length,
        .stride=padded_item_size,
        .atomic=arr->atomic,
    };
}

public void Array$insert(Array_t *arr, const void *item, Int_t int_index, int64_t padded_item_size)
{
    int64_t index = Int_to_Int64(int_index, false);
    if (index <= 0) index = arr->length + index + 1;

    if (index < 1) index = 1;
    else if (index > (int64_t)arr->length + 1)
        fail("Invalid insertion index %ld for an array with length %ld", index, arr->length);

    if (!arr->data) {
        arr->free = 4;
        arr->data = arr->atomic ? GC_MALLOC_ATOMIC((size_t)arr->free * (size_t)padded_item_size)
            : GC_MALLOC((size_t)arr->free * (size_t)padded_item_size);
        arr->stride = padded_item_size;
    } else if (arr->free < 1 || arr->data_refcount != 0 || (int64_t)arr->stride != padded_item_size) {
        // Resize policy: +50% growth (clamped between 8 and ARRAY_MAX_FREE_ENTRIES)
        arr->free = MIN(ARRAY_MAX_FREE_ENTRIES, MAX(8, arr->length)/2);
        void *copy = arr->atomic ? GC_MALLOC_ATOMIC((size_t)(arr->length + arr->free) * (size_t)padded_item_size)
            : GC_MALLOC((size_t)(arr->length + arr->free) * (size_t)padded_item_size);
        for (int64_t i = 0; i < index-1; i++)
            memcpy(copy + i*padded_item_size, arr->data + arr->stride*i, (size_t)padded_item_size);
        for (int64_t i = index-1; i < (int64_t)arr->length; i++)
            memcpy(copy + (i+1)*padded_item_size, arr->data + arr->stride*i, (size_t)padded_item_size);
        arr->data = copy;
        arr->data_refcount = 0;
        arr->stride = padded_item_size;
    } else {
        if (index != arr->length+1)
            memmove(
                arr->data + index*padded_item_size,
                arr->data + (index-1)*padded_item_size,
                (size_t)((arr->length - index + 1)*padded_item_size));
    }
    assert(arr->free > 0);
    --arr->free;
    ++arr->length;
    memcpy((void*)arr->data + (index-1)*padded_item_size, item, (size_t)padded_item_size);
}

public void Array$insert_all(Array_t *arr, Array_t to_insert, Int_t int_index, int64_t padded_item_size)
{
    int64_t index = Int_to_Int64(int_index, false);
    if (to_insert.length == 0)
        return;

    if (!arr->data) {
        *arr = to_insert;
        ARRAY_INCREF(*arr);
        return;
    }

    if (index < 1) index = arr->length + index + 1;

    if (index < 1) index = 1;
    else if (index > (int64_t)arr->length + 1)
        fail("Invalid insertion index %ld for an array with length %ld", index, arr->length);

    if ((int64_t)arr->free >= (int64_t)to_insert.length // Adequate free space
        && arr->data_refcount == 0 // Not aliased memory
        && (int64_t)arr->stride == padded_item_size) { // Contiguous array
        // If we can fit this within the array's preallocated free space, do that:
        arr->free -= to_insert.length;
        arr->length += to_insert.length;
        if (index != arr->length+1)
            memmove((void*)arr->data + index*padded_item_size,
                    arr->data + (index-1)*padded_item_size,
                    (size_t)((arr->length - index + to_insert.length-1)*padded_item_size));
        for (int64_t i = 0; i < to_insert.length; i++)
            memcpy((void*)arr->data + (index-1 + i)*padded_item_size,
                   to_insert.data + i*to_insert.stride, (size_t)padded_item_size);
    } else {
        // Otherwise, allocate a new chunk of memory for the array and populate it:
        int64_t new_len = arr->length + to_insert.length;
        arr->free = MIN(ARRAY_MAX_FREE_ENTRIES, MAX(8, new_len/4));
        void *data = arr->atomic ? GC_MALLOC_ATOMIC((size_t)((new_len + arr->free) * padded_item_size))
            : GC_MALLOC((size_t)((new_len + arr->free) * padded_item_size));
        void *p = data;

        // Copy first chunk of `arr` if needed:
        if (index > 1) {
            if (arr->stride == padded_item_size) {
                p = mempcpy(p, arr->data, (size_t)((index-1)*padded_item_size));
            } else {
                for (int64_t i = 0; i < index-1; i++)
                    p = mempcpy(p, arr->data + arr->stride*i, (size_t)padded_item_size);
            }
        }

        // Copy `to_insert`
        if (to_insert.stride == padded_item_size) {
            p = mempcpy(p, to_insert.data, (size_t)(to_insert.length*padded_item_size));
        } else {
            for (int64_t i = 0; i < index-1; i++)
                p = mempcpy(p, to_insert.data + to_insert.stride*i, (size_t)padded_item_size);
        }

        // Copy last chunk of `arr` if needed:
        if (index < arr->length + 1) {
            if (arr->stride == padded_item_size) {
                p = mempcpy(p, arr->data + padded_item_size*(index-1), (size_t)((arr->length - index + 1)*padded_item_size));
            } else {
                for (int64_t i = index-1; i < arr->length-1; i++)
                    p = mempcpy(p, arr->data + arr->stride*i, (size_t)padded_item_size);
            }
        }
        arr->length = new_len;
        arr->stride = padded_item_size;
        arr->data = data;
        arr->data_refcount = 0;
    }
}

public void Array$remove_at(Array_t *arr, Int_t int_index, Int_t int_count, int64_t padded_item_size)
{
    int64_t index = Int_to_Int64(int_index, false);
    if (index < 1) index = arr->length + index + 1;

    int64_t count = Int_to_Int64(int_count, false);
    if (index < 1 || index > (int64_t)arr->length || count < 1) return;

    if (count > arr->length - index + 1)
        count = (arr->length - index) + 1;

    if (index == 1) {
        arr->data += arr->stride * count;
    } else if (index + count > arr->length) {
        if (arr->free >= 0)
            arr->free += count;
    } else if (arr->data_refcount != 0 || (int64_t)arr->stride != padded_item_size) {
        void *copy = arr->atomic ? GC_MALLOC_ATOMIC((size_t)((arr->length-1) * padded_item_size))
            : GC_MALLOC((size_t)((arr->length-1) * padded_item_size));
        for (int64_t src = 1, dest = 1; src <= (int64_t)arr->length; src++) {
            if (src < index || src >= index + count) {
                memcpy(copy + (dest - 1)*padded_item_size, arr->data + arr->stride*(src - 1), (size_t)padded_item_size);
                ++dest;
            }
        }
        arr->data = copy;
        arr->free = 0;
        arr->data_refcount = 0;
    } else {
        memmove((void*)arr->data + (index-1)*padded_item_size, arr->data + (index-1 + count)*padded_item_size,
                (size_t)((arr->length - index + count - 1)*padded_item_size));
        arr->free += count;
    }
    arr->length -= count;
    if (arr->length == 0) arr->data = NULL;
}

public void Array$remove_item(Array_t *arr, void *item, Int_t max_removals, const TypeInfo_t *type)
{
    int64_t padded_item_size = get_padded_item_size(type);
    const Int_t ZERO = (Int_t){.small=(0<<2)|1};
    const Int_t ONE = (Int_t){.small=(1<<2)|1};
    const TypeInfo_t *item_type = type->ArrayInfo.item;
    for (int64_t i = 0; i < arr->length; ) {
        if (max_removals.small == ZERO.small) // zero
            break;

        if (generic_equal(item, arr->data + i*arr->stride, item_type)) {
            Array$remove_at(arr, I(i+1), ONE, padded_item_size);
            max_removals = Int$minus(max_removals, ONE);
        } else {
            i++;
        }
    }
}

public Int_t Array$find(Array_t arr, void *item, const TypeInfo_t *type)
{
    const TypeInfo_t *item_type = type->ArrayInfo.item;
    for (int64_t i = 0; i < arr.length; i++) {
        if (generic_equal(item, arr.data + i*arr.stride, item_type))
            return I(i+1);
    }
    return NONE_INT;
}

public Int_t Array$first(Array_t arr, Closure_t predicate)
{
    bool (*is_good)(void*, void*) = (void*)predicate.fn;
    for (int64_t i = 0; i < arr.length; i++) {
        if (is_good(arr.data + i*arr.stride, predicate.userdata))
            return I(i+1);
    }
    return NONE_INT;
}

public void Array$sort(Array_t *arr, Closure_t comparison, int64_t padded_item_size)
{
    if (arr->data_refcount != 0 || (int64_t)arr->stride != padded_item_size)
        Array$compact(arr, padded_item_size);

    qsort_r(arr->data, (size_t)arr->length, (size_t)padded_item_size, comparison.fn, comparison.userdata);
}

public Array_t Array$sorted(Array_t arr, Closure_t comparison, int64_t padded_item_size)
{
    Array$compact(&arr, padded_item_size);
    qsort_r(arr.data, (size_t)arr.length, (size_t)padded_item_size, comparison.fn, comparison.userdata);
    return arr;
}

public void Array$shuffle(Array_t *arr, RNG_t rng, int64_t padded_item_size)
{
    if (arr->data_refcount != 0 || (int64_t)arr->stride != padded_item_size)
        Array$compact(arr, padded_item_size);

    char tmp[padded_item_size];
    for (int64_t i = arr->length-1; i > 1; i--) {
        int64_t j = RNG$int64(rng, 0, i);
        memcpy(tmp, arr->data + i*padded_item_size, (size_t)padded_item_size);
        memcpy((void*)arr->data + i*padded_item_size, arr->data + j*padded_item_size, (size_t)padded_item_size);
        memcpy((void*)arr->data + j*padded_item_size, tmp, (size_t)padded_item_size);
    }
}

public Array_t Array$shuffled(Array_t arr, RNG_t rng, int64_t padded_item_size)
{
    Array$compact(&arr, padded_item_size);
    Array$shuffle(&arr, rng, padded_item_size);
    return arr;
}

public void *Array$random(Array_t arr, RNG_t rng)
{
    if (arr.length == 0)
        return NULL; // fail("Cannot get a random item from an empty array!");

    int64_t index = RNG$int64(rng, 0, arr.length-1);
    return arr.data + arr.stride*index;
}

public Table_t Array$counts(Array_t arr, const TypeInfo_t *type)
{
    Table_t counts = {};
    const TypeInfo_t count_type = *Table$info(type->ArrayInfo.item, &Int$info);
    for (int64_t i = 0; i < arr.length; i++) {
        void *key = arr.data + i*arr.stride;
        int64_t *count = Table$get(counts, key, &count_type);
        int64_t val = count ? *count + 1 : 1;
        Table$set(&counts, key, &val, &count_type);
    }
    return counts;
}

public Array_t Array$sample(Array_t arr, Int_t int_n, Array_t weights, RNG_t rng, int64_t padded_item_size)
{
    int64_t n = Int_to_Int64(int_n, false);
    if (n < 0)
        fail("Cannot select a negative number of values");

    if (n == 0)
        return (Array_t){};

    if (arr.length == 0)
        fail("There are no elements in this array!");

    Array_t selected = {
        .data=arr.atomic ? GC_MALLOC_ATOMIC((size_t)(n * padded_item_size)) : GC_MALLOC((size_t)(n * padded_item_size)),
        .length=n,
        .stride=padded_item_size, .atomic=arr.atomic};

    if (weights.length < 0) {
        for (int64_t i = 0; i < n; i++) {
            int64_t index = RNG$int64(rng, 0, arr.length-1);
            memcpy(selected.data + i*padded_item_size, arr.data + arr.stride*index, (size_t)padded_item_size);
        }
        return selected;
    }

    if (weights.length != arr.length)
        fail("Array has %ld elements, but there are %ld weights given", arr.length, weights.length);

    double total = 0.0;
    for (int64_t i = 0; i < weights.length && i < arr.length; i++) {
        double weight = *(double*)(weights.data + weights.stride*i);
        if (isinf(weight))
            fail("Infinite weight!");
        else if (isnan(weight))
            fail("NaN weight!");
        else if (weight < 0.0)
            fail("Negative weight!");
        else
            total += weight;
    }

    if (isinf(total))
        fail("Sample weights have overflowed to infinity");

    if (total == 0.0)
        fail("None of the given weights are nonzero");

    double inverse_average = (double)arr.length / total;

    struct {
        int64_t alias;
        double odds;
    } aliases[arr.length] = {};

    for (int64_t i = 0; i < arr.length; i++) {
        double weight = i >= weights.length ? 0.0 : *(double*)(weights.data + weights.stride*i);
        aliases[i].odds = weight * inverse_average;
        aliases[i].alias = -1;
    }

    int64_t small = 0;
    for (int64_t big = 0; big < arr.length; big++) {
        while (aliases[big].odds >= 1.0) {
            while (small < arr.length && (aliases[small].odds >= 1.0 || aliases[small].alias != -1))
                ++small;

            if (small >= arr.length) {
                aliases[big].odds = 1.0;
                aliases[big].alias = big;
                break;
            }

            aliases[small].alias = big;
            aliases[big].odds = (aliases[small].odds + aliases[big].odds) - 1.0;
        }
        if (big < small) small = big;
    }

    for (int64_t i = small; i < arr.length; i++)
        if (aliases[i].alias == -1)
            aliases[i].alias = i;

    for (int64_t i = 0; i < n; i++) {
        double r = RNG$num(rng, 0, arr.length);
        int64_t index = (int64_t)r;
        if ((r - (double)index) > aliases[index].odds)
            index = aliases[index].alias;
        memcpy(selected.data + i*selected.stride, arr.data + index*arr.stride, (size_t)padded_item_size);
    }
    return selected;
}

public Array_t Array$from(Array_t array, Int_t first)
{
    return Array$slice(array, first, I_small(-1));
}

public Array_t Array$to(Array_t array, Int_t last)
{
    return Array$slice(array, I_small(1), last);
}

public Array_t Array$by(Array_t array, Int_t int_stride, int64_t padded_item_size)
{
    int64_t stride = Int_to_Int64(int_stride, false);
    // In the unlikely event that the stride value would be too large to fit in
    // a 15-bit integer, fall back to creating a copy of the array:
    if (unlikely(array.stride*stride < ARRAY_MIN_STRIDE || array.stride*stride > ARRAY_MAX_STRIDE)) {
        void *copy = NULL;
        int64_t len = (stride < 0 ? array.length / -stride : array.length / stride) + ((array.length % stride) != 0);
        if (len > 0) {
            copy = array.atomic ? GC_MALLOC_ATOMIC((size_t)(len * padded_item_size)) : GC_MALLOC((size_t)(len * padded_item_size));
            void *start = (stride < 0 ? array.data + (array.stride * (array.length - 1)) : array.data);
            for (int64_t i = 0; i < len; i++)
                memcpy(copy + i*padded_item_size, start + array.stride*stride*i, (size_t)padded_item_size);
        }
        return (Array_t){
            .data=copy,
            .length=len,
            .stride=padded_item_size,
            .atomic=array.atomic,
        };
    }

    if (stride == 0)
        return (Array_t){.atomic=array.atomic};

    return (Array_t){
        .atomic=array.atomic,
        .data=(stride < 0 ? array.data + (array.stride * (array.length - 1)) : array.data),
        .length=(stride < 0 ? array.length / -stride : array.length / stride) + ((array.length % stride) != 0),
        .stride=array.stride * stride,
        .data_refcount=array.data_refcount,
    };
}

public Array_t Array$slice(Array_t array, Int_t int_first, Int_t int_last)

{
    int64_t first = Int_to_Int64(int_first, false);
    if (first < 0)
        first = array.length + first + 1;

    int64_t last = Int_to_Int64(int_last, false);
    if (last < 0)
        last = array.length + last + 1;

    if (last > array.length)
        last = array.length;

    if (first < 1 || first > array.length || last == 0)
        return (Array_t){.atomic=array.atomic};

    return (Array_t){
        .atomic=array.atomic,
        .data=array.data + array.stride*(first-1),
        .length=last - first + 1,
        .stride=array.stride,
        .data_refcount=array.data_refcount,
    };
}

public Array_t Array$reversed(Array_t array, int64_t padded_item_size)
{
    // Just in case negating the stride gives a value that doesn't fit into a
    // 15-bit integer, fall back to Array$by()'s more general method of copying
    // the array. This should only happen if array.stride is MIN_STRIDE to
    // begin with (very unlikely).
    if (unlikely(-array.stride < ARRAY_MIN_STRIDE || -array.stride > ARRAY_MAX_STRIDE))
        return Array$by(array, I(-1), padded_item_size);

    Array_t reversed = array;
    reversed.stride = -array.stride;
    reversed.data = array.data + (array.length-1)*array.stride;
    return reversed;
}

public Array_t Array$concat(Array_t x, Array_t y, int64_t padded_item_size)
{
    void *data = x.atomic ? GC_MALLOC_ATOMIC((size_t)(padded_item_size*(x.length + y.length)))
        : GC_MALLOC((size_t)(padded_item_size*(x.length + y.length)));
    if (x.stride == padded_item_size) {
        memcpy(data, x.data, (size_t)(padded_item_size*x.length));
    } else {
        for (int64_t i = 0; i < x.length; i++)
            memcpy(data + i*padded_item_size, x.data + i*padded_item_size, (size_t)padded_item_size);
    }

    void *dest = data + padded_item_size*x.length;
    if (y.stride == padded_item_size) {
        memcpy(dest, y.data, (size_t)(padded_item_size*y.length));
    } else {
        for (int64_t i = 0; i < y.length; i++)
            memcpy(dest + i*padded_item_size, y.data + i*y.stride, (size_t)padded_item_size);
    }

    return (Array_t){
        .data=data,
        .length=x.length + y.length,
        .stride=padded_item_size,
        .atomic=x.atomic,
    };
}

public bool Array$has(Array_t array, void *item, const TypeInfo_t *type)
{
    const TypeInfo_t *item_type = type->ArrayInfo.item;
    for (int64_t i = 0; i < array.length; i++) {
        if (generic_equal(array.data + i*array.stride, item, item_type))
            return true;
    }
    return false;
}

public void Array$clear(Array_t *array)
{
    *array = (Array_t){.data=0, .length=0};
}

public int32_t Array$compare(const void *vx, const void *vy, const TypeInfo_t *type)
{
    const Array_t *x = (Array_t*)vx, *y = (Array_t*)vy;
    // Early out for arrays with the same data, e.g. two copies of the same array:
    if (x->data == y->data && x->stride == y->stride)
        return (x->length > y->length) - (x->length < y->length);

    const TypeInfo_t *item = type->ArrayInfo.item;
    if (item->tag == PointerInfo || !item->metamethods.compare) { // data comparison
        int64_t item_padded_size = type->ArrayInfo.item->size;
        if (type->ArrayInfo.item->align > 1 && item_padded_size % type->ArrayInfo.item->align)
            errx(1, "Item size is not padded!");

        if ((int64_t)x->stride == item_padded_size && (int64_t)y->stride == item_padded_size && item->size == item_padded_size) {
            int32_t cmp = (int32_t)memcmp(x->data, y->data, (size_t)(MIN(x->length, y->length)*item_padded_size));
            if (cmp != 0) return cmp;
        } else {
            for (int32_t i = 0, len = MIN(x->length, y->length); i < len; i++) {
                int32_t cmp = (int32_t)memcmp(x->data+ x->stride*i, y->data + y->stride*i, (size_t)(item->size));
                if (cmp != 0) return cmp;
            }
        }
    } else {
        for (int32_t i = 0, len = MIN(x->length, y->length); i < len; i++) {
            int32_t cmp = generic_compare(x->data + x->stride*i, y->data + y->stride*i, item);
            if (cmp != 0) return cmp;
        }
    }
    return (x->length > y->length) - (x->length < y->length);
}

public bool Array$equal(const void *x, const void *y, const TypeInfo_t *type)
{
    return x == y || (((Array_t*)x)->length == ((Array_t*)y)->length && Array$compare(x, y, type) == 0);
}

public Text_t Array$as_text(const void *obj, bool colorize, const TypeInfo_t *type)
{
    Array_t *arr = (Array_t*)obj;
    if (!arr)
        return Text$concat(Text("["), generic_as_text(NULL, false, type->ArrayInfo.item), Text("]"));

    const TypeInfo_t *item_type = type->ArrayInfo.item;
    Text_t text = Text("[");
    for (int64_t i = 0; i < arr->length; i++) {
        if (i > 0)
            text = Text$concat(text, Text(", "));
        Text_t item_text = generic_as_text(arr->data + i*arr->stride, colorize, item_type);
        text = Text$concat(text, item_text);
    }
    text = Text$concat(text, Text("]"));
    return text;
}

public uint64_t Array$hash(const void *obj, const TypeInfo_t *type)
{
    const Array_t *arr = (Array_t*)obj;
    const TypeInfo_t *item = type->ArrayInfo.item;
    siphash sh;
    siphashinit(&sh, sizeof(uint64_t[arr->length]));
    if (item->tag == PointerInfo || (!item->metamethods.hash && item->size == sizeof(void*))) { // Raw data hash
        for (int64_t i = 0; i < arr->length; i++)
            siphashadd64bits(&sh, (uint64_t)(arr->data + i*arr->stride));
    } else {
        for (int64_t i = 0; i < arr->length; i++) {
            uint64_t item_hash = generic_hash(arr->data + i*arr->stride, item);
            siphashadd64bits(&sh, item_hash);
        }
    }
    return siphashfinish_last_part(&sh, 0);
}

static void siftdown(Array_t *heap, int64_t startpos, int64_t pos, Closure_t comparison, int64_t padded_item_size)
{
    assert(pos > 0 && pos < heap->length);
    char newitem[padded_item_size];
    memcpy(newitem, heap->data + heap->stride*pos, (size_t)(padded_item_size));
    while (pos > startpos) {
        int64_t parentpos = (pos - 1) >> 1;
        typedef int32_t (*cmp_fn_t)(void*, void*, void*);
        int32_t cmp = ((cmp_fn_t)comparison.fn)(newitem, heap->data + heap->stride*parentpos, comparison.userdata);
        if (cmp >= 0)
            break;

        memcpy(heap->data + heap->stride*pos, heap->data + heap->stride*parentpos, (size_t)(padded_item_size));
        pos = parentpos;
    }
    memcpy(heap->data + heap->stride*pos, newitem, (size_t)(padded_item_size));
}

static void siftup(Array_t *heap, int64_t pos, Closure_t comparison, int64_t padded_item_size)
{
    int64_t endpos = heap->length;
    int64_t startpos = pos;
    assert(pos < endpos);

    char old_top[padded_item_size];
    memcpy(old_top, heap->data + heap->stride*pos, (size_t)(padded_item_size));
    // Bubble up the smallest leaf node
    int64_t limit = endpos >> 1;
    while (pos < limit) {
        int64_t childpos = 2*pos + 1; // Smaller of the two child nodes
        if (childpos + 1 < endpos) {
            typedef int32_t (*cmp_fn_t)(void*, void*, void*);
            int32_t cmp = ((cmp_fn_t)comparison.fn)(
                heap->data + heap->stride*childpos,
                heap->data + heap->stride*(childpos + 1),
                comparison.userdata);
            childpos += (cmp >= 0);
        }

        // Move the child node up:
        memcpy(heap->data + heap->stride*pos, heap->data + heap->stride*childpos, (size_t)(padded_item_size));
        pos = childpos;
    }
    memcpy(heap->data + heap->stride*pos, old_top, (size_t)(padded_item_size));
    // Shift the node's parents down:
    siftdown(heap, startpos, pos, comparison, padded_item_size);
}

public void Array$heap_push(Array_t *heap, const void *item, Closure_t comparison, int64_t padded_item_size)
{
    Array$insert(heap, item, I(0), padded_item_size);

    if (heap->length > 1) {
        if (heap->data_refcount != 0)
            Array$compact(heap, padded_item_size);
        siftdown(heap, 0, heap->length-1, comparison, padded_item_size);
    }
}

public void Array$heap_pop(Array_t *heap, Closure_t comparison, int64_t padded_item_size)
{
    if (heap->length == 0)
        fail("Attempt to pop from an empty array");

    if (heap->length == 1) {
        *heap = (Array_t){};
    } else if (heap->length == 2) {
        heap->data += heap->stride;
        --heap->length;
    } else {
        if (heap->data_refcount != 0)
            Array$compact(heap, padded_item_size);
        memcpy(heap->data, heap->data + heap->stride*(heap->length-1), (size_t)(padded_item_size));
        --heap->length;
        siftup(heap, 0, comparison, padded_item_size);
    }
}

public void Array$heapify(Array_t *heap, Closure_t comparison, int64_t padded_item_size)
{
    if (heap->data_refcount != 0)
        Array$compact(heap, padded_item_size);

    // It's necessary to bump the refcount because the user's comparison
    // function could do stuff that modifies the heap's data.
    ARRAY_INCREF(*heap);
    int64_t i, n = heap->length;
    for (i = (n >> 1) - 1 ; i >= 0 ; i--)
        siftup(heap, i, comparison, padded_item_size);
    ARRAY_DECREF(*heap);
}

public Int_t Array$binary_search(Array_t array, void *target, Closure_t comparison)
{
    typedef int32_t (*cmp_fn_t)(void*, void*, void*);
    int64_t lo = 0, hi = array.length-1;
    while (lo <= hi) {
        int64_t mid = (lo + hi) / 2;
        int32_t cmp = ((cmp_fn_t)comparison.fn)(
            array.data + array.stride*mid, target, comparison.userdata);
        if (cmp == 0)
            return I(mid+1);
        else if (cmp < 0)
            lo = mid + 1;
        else if (cmp > 0)
            hi = mid - 1;
    }
    return I(lo+1); // Return the index where the target would be inserted
}

public PUREFUNC bool Array$is_none(const void *obj, const TypeInfo_t*)
{
    return ((Array_t*)obj)->length < 0;
}

public void Array$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type)
{
    Array_t arr = *(Array_t*)obj;
    int64_t len = arr.length;
    Int64$serialize(&len, out, pointers, &Int64$info);
    auto item_serialize = type->ArrayInfo.item->metamethods.serialize;
    if (item_serialize) {
        for (int64_t i = 0; i < len; i++)
            item_serialize(arr.data + i*arr.stride, out, pointers, type->ArrayInfo.item);
    } else if (arr.stride == type->ArrayInfo.item->size) {
        fwrite(arr.data, (size_t)type->ArrayInfo.item->size, (size_t)len, out);
    } else {
        for (int64_t i = 0; i < len; i++)
            fwrite(arr.data + i*arr.stride, (size_t)type->ArrayInfo.item->size, 1, out);
    }
}

public void Array$deserialize(FILE *in, void *obj, Array_t *pointers, const TypeInfo_t *type)
{
    int64_t len = -1;
    Int64$deserialize(in, &len, pointers, &Int64$info);
    int64_t padded_size = type->ArrayInfo.item->size;
    if (type->ArrayInfo.item->align > 0 && padded_size % type->ArrayInfo.item->align > 0)
        padded_size += type->ArrayInfo.item->align - (padded_size % type->ArrayInfo.item->align);
    Array_t arr = {
        .length=len,
        .data=GC_MALLOC((size_t)(len*padded_size)),
        .stride=padded_size,
    };
    auto item_deserialize = type->ArrayInfo.item->metamethods.deserialize;
    if (item_deserialize) {
        for (int64_t i = 0; i < len; i++)
            item_deserialize(in, arr.data + i*arr.stride, pointers, type->ArrayInfo.item);
    } else if (arr.stride == type->ArrayInfo.item->size) {
        fread(arr.data, (size_t)type->ArrayInfo.item->size, (size_t)len, in);
    } else {
        for (int64_t i = 0; i < len; i++)
            fread(arr.data + i*arr.stride, (size_t)type->ArrayInfo.item->size, 1, in);
    }
    *(Array_t*)obj = arr;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
