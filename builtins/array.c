// Functions that operate on arrays

#include <ctype.h>
#include <err.h>
#include <gc.h>
#include <gc/cord.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>

#include "array.h"
#include "functions.h"
#include "halfsiphash.h"
#include "types.h"
#include "util.h"

static inline size_t get_item_size(const TypeInfo *info)
{
    return info->ArrayInfo.item->size;
}

// Replace the array's .data pointer with a new pointer to a copy of the
// data that is compacted and has a stride of exactly `item_size`
public void Array$compact(array_t *arr, const TypeInfo *type)
{
    void *copy = NULL;
    int64_t item_size = get_item_size(type);
    if (arr->length > 0) {
        copy = arr->atomic ? GC_MALLOC_ATOMIC(arr->length * item_size) : GC_MALLOC(arr->length * item_size);
        if ((int64_t)arr->stride == item_size) {
            memcpy(copy, arr->data, arr->length * item_size);
        } else {
            for (int64_t i = 0; i < arr->length; i++)
                memcpy(copy + i*item_size, arr->data + arr->stride*i, item_size);
        }
    }
    *arr = (array_t){
        .data=copy,
        .length=arr->length,
        .stride=item_size,
        .atomic=arr->atomic,
    };
}

public void Array$insert(array_t *arr, const void *item, int64_t index, const TypeInfo *type)
{
    if (index <= 0) index = arr->length + index + 1;

    if (index < 1) index = 1;
    else if (index > (int64_t)arr->length + 1) index = (int64_t)arr->length + 1;

    int64_t item_size = get_item_size(type);
    if (!arr->data) {
        arr->free = 4;
        arr->data = arr->atomic ? GC_MALLOC_ATOMIC(arr->free * item_size) : GC_MALLOC(arr->free * item_size);
        arr->stride = item_size;
    } else if (arr->free < 1 || arr->data_refcount || (int64_t)arr->stride != item_size) {
        arr->free = MAX(15, MIN(1, arr->length/4));
        void *copy = arr->atomic ? GC_MALLOC_ATOMIC((arr->length + arr->free) * item_size) : GC_MALLOC((arr->length + arr->free) * item_size);
        for (int64_t i = 0; i < index-1; i++)
            memcpy(copy + i*item_size, arr->data + arr->stride*i, item_size);
        for (int64_t i = index-1; i < (int64_t)arr->length; i++)
            memcpy(copy + (i+1)*item_size, arr->data + arr->stride*i, item_size);
        arr->data = copy;
        arr->data_refcount = 0;
        arr->stride = item_size;
    } else {
        if (index != arr->length+1)
            memmove((void*)arr->data + index*item_size, arr->data + (index-1)*item_size, (arr->length - index)*item_size);
    }
    assert(arr->free > 0);
    --arr->free;
    ++arr->length;
    memcpy((void*)arr->data + (index-1)*item_size, item, item_size);
}

public void Array$insert_all(array_t *arr, array_t to_insert, int64_t index, const TypeInfo *type)
{
    if (index < 1) index = arr->length + index + 1;

    if (index < 1) index = 1;
    else if (index > (int64_t)arr->length + 1) index = (int64_t)arr->length + 1;

    int64_t item_size = get_item_size(type);
    if (!arr->data) {
        arr->free = to_insert.length;
        arr->data = arr->atomic ? GC_MALLOC_ATOMIC(item_size*arr->free) : GC_MALLOC(item_size*arr->free);
    } else if ((int64_t)arr->free < (int64_t)to_insert.length || arr->data_refcount || (int64_t)arr->stride != item_size) {
        arr->free = to_insert.length;
        void *copy = arr->atomic ? GC_MALLOC_ATOMIC((arr->length + arr->free) * item_size) : GC_MALLOC((arr->length + arr->free) * item_size);
        for (int64_t i = 0; i < index-1; i++)
            memcpy(copy + i*item_size, arr->data + arr->stride*i, item_size);
        for (int64_t i = index-1; i < (int64_t)arr->length; i++)
            memcpy(copy + (i+to_insert.length)*item_size, arr->data + arr->stride*i, item_size);
        arr->data = copy;
        arr->data_refcount = 0;
    } else {
        if (index != arr->length+1)
            memmove((void*)arr->data + index*item_size, arr->data + (index-1)*item_size, (arr->length - index + to_insert.length-1)*item_size);
    }
    arr->free -= to_insert.length;
    arr->length += to_insert.length;
    for (int64_t i = 0; i < to_insert.length; i++)
        memcpy((void*)arr->data + (index-1 + i)*item_size, to_insert.data + i*to_insert.stride, item_size);
}

public void Array$remove(array_t *arr, int64_t index, int64_t count, const TypeInfo *type)
{
    if (index < 1) index = arr->length + index + 1;

    if (index < 1 || index > (int64_t)arr->length || count < 1) return;

    if (count > arr->length - index + 1)
        count = (arr->length - index) + 1;

    // TODO: optimize arr.remove(1) by just updating the .data and .length values

    int64_t item_size = get_item_size(type);
    if (index + count > arr->length) {
        if (arr->free >= 0)
            arr->free += count;
    } else if (arr->data_refcount || (int64_t)arr->stride != item_size) {
        void *copy = arr->atomic ? GC_MALLOC_ATOMIC((arr->length-1) * item_size) : GC_MALLOC((arr->length-1) * item_size);
        for (int64_t src = 1, dest = 1; src <= (int64_t)arr->length; src++) {
            if (src < index || src >= index + count) {
                memcpy(copy + (dest - 1)*item_size, arr->data + arr->stride*(src - 1), item_size);
                ++dest;
            }
        }
        arr->data = copy;
        arr->free = 0;
        arr->data_refcount = 0;
    } else {
        memmove((void*)arr->data + (index-1)*item_size, arr->data + (index-1 + count)*item_size, (arr->length - index + count - 1)*item_size);
        arr->free += count;
    }
    arr->length -= count;
}

public void Array$sort(array_t *arr, closure_t comparison, const TypeInfo *type)
{
    const TypeInfo *item_type = type->ArrayInfo.item;
    int64_t item_size = item_type->size;
    if (item_type->align > 1 && item_size % item_type->align)
        item_size += item_type->align - (item_size % item_type->align); // padding

    if (arr->data_refcount || (int64_t)arr->stride != item_size)
        Array$compact(arr, type);

    qsort_r(arr->data, arr->length, item_size, comparison.fn, comparison.userdata);
}

public array_t Array$sorted(array_t arr, closure_t comparison, const TypeInfo *type)
{
    arr.data_refcount = 3;
    Array$sort(&arr, comparison, type);
    return arr;
}

public void Array$shuffle(array_t *arr, const TypeInfo *type)
{
    int64_t item_size = get_item_size(type);
    if (arr->data_refcount || (int64_t)arr->stride != item_size)
        Array$compact(arr, type);

    char tmp[item_size];
    for (int64_t i = arr->length-1; i > 1; i--) {
        int32_t j = arc4random_uniform(i+1);
        memcpy(tmp, arr->data + i*item_size, item_size);
        memcpy((void*)arr->data + i*item_size, arr->data + j*item_size, item_size);
        memcpy((void*)arr->data + j*item_size, tmp, item_size);
    }
}

public void *Array$random(array_t arr)
{
    if (arr.length == 0)
        return NULL; // fail("Cannot get a random item from an empty array!");
    uint32_t index = arc4random_uniform(arr.length);
    return arr.data + arr.stride*index;
}

public array_t Array$sample(array_t arr, int64_t n, array_t weights, const TypeInfo *type)
{
    if (arr.length == 0 || n <= 0)
        return (array_t){};

    int64_t item_size = get_item_size(type);
    array_t selected = {
        .data=arr.atomic ? GC_MALLOC_ATOMIC(n * item_size) : GC_MALLOC(n * item_size),
        .length=n,
        .stride=item_size, .atomic=arr.atomic};

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

    if (total == 0.0) {
        for (int64_t i = 0; i < n; i++) {
            uint32_t index = arc4random_uniform(arr.length);
            memcpy(selected.data + i*item_size, arr.data + arr.stride*index, item_size);
        }
    } else {
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
            double r = drand48() * arr.length;
            int64_t index = (int64_t)r;
            if ((r - (double)index) > aliases[index].odds)
                index = aliases[index].alias;
            memcpy(selected.data + i*selected.stride, arr.data + index*arr.stride, item_size);
        }
    }
    return selected;
}

public array_t Array$slice(array_t *array, int64_t first, int64_t length, int64_t stride, const TypeInfo *type)
{
    if (stride > MAX_STRIDE || stride < MIN_STRIDE)
        fail("Stride is too big: %ld", stride);

    if (stride == 0 || length <= 0) {
        // Zero stride
        return (array_t){.atomic=array->atomic};
    } else if (stride < 0) {
        if (first == INT64_MIN) first = array->length;
        if (first > array->length) {
            // Range starting after array
            int64_t residual = first % -stride;
            first = array->length - (array->length % -stride) + residual;
        }
        if (first > array->length) first += stride;
        if (first < 1) {
            // Range outside array
            return (array_t){.atomic=array->atomic};
        }
    } else {
        if (first == INT64_MIN) first = 1;
        if (first < 1) {
            // Range starting before array
            first = first % stride;
        }
        while (first < 1) first += stride;
        if (first > array->length) {
            // Range outside array
            return (array_t){.atomic=array->atomic};
        }
    }

    if (length > array->length/labs(stride) + 1) length = array->length/labs(stride) + 1;
    if (length < 0) length = -length;

    // Saturating add:
    array->data_refcount |= (array->data_refcount << 1) | 1;

    int64_t item_size = get_item_size(type);
    return (array_t){
        .atomic=array->atomic,
        .data=array->data + item_size*(first-1),
        .length=length,
        .stride=(array->stride * stride),
        .data_refcount=array->data_refcount,
    };
}

public array_t Array$reversed(array_t array)
{
    array_t reversed = array;
    reversed.stride = -array.stride;
    reversed.data = array.data + (array.length-1)*array.stride;
    return reversed;
}

public array_t Array$concat(array_t x, array_t y, const TypeInfo *type)
{
    int64_t item_size = get_item_size(type);
    void *data = x.atomic ? GC_MALLOC_ATOMIC(item_size*(x.length + y.length)) : GC_MALLOC(item_size*(x.length + y.length));
    if (x.stride == item_size) {
        memcpy(data, x.data, item_size*x.length);
    } else {
        for (int64_t i = 0; i < x.length; i++)
            memcpy(data + i*item_size, x.data + i*item_size, item_size);
    }

    if (y.stride == item_size) {
        memcpy(data + item_size*x.length, y.data, item_size*y.length);
    } else {
        for (int64_t i = 0; i < x.length; i++)
            memcpy(data + (x.length + i)*item_size, y.data + i*item_size, item_size);
    }

    return (array_t){
        .data=data,
        .length=x.length + y.length,
        .stride=item_size,
        .atomic=x.atomic,
    };
}

public bool Array$contains(array_t array, void *item, const TypeInfo *type)
{
    const TypeInfo *item_type = type->ArrayInfo.item;
    for (int64_t i = 0; i < array.length; i++)
        if (generic_equal(array.data + i*array.stride, item, item_type))
            return true;
    return false;
}

public void Array$clear(array_t *array)
{
    *array = (array_t){.data=0, .length=0};
}

public int32_t Array$compare(const array_t *x, const array_t *y, const TypeInfo *type)
{
    // Early out for arrays with the same data, e.g. two copies of the same array:
    if (x->data == y->data && x->stride == y->stride)
        return (x->length > y->length) - (x->length < y->length);

    const TypeInfo *item = type->ArrayInfo.item;
    if (item->tag == PointerInfo || (item->tag == CustomInfo && item->CustomInfo.compare == NULL)) { // data comparison
        int64_t item_size = item->size;
        if (x->stride == (int32_t)item_size && y->stride == (int32_t)item_size) {
            int32_t cmp = (int32_t)memcmp(x->data, y->data, MIN(x->length, y->length)*item_size);
            if (cmp != 0) return cmp;
        } else {
            for (int32_t i = 0, len = MIN(x->length, y->length); i < len; i++) {
                int32_t cmp = (int32_t)memcmp(x->data+ x->stride*i, y->data + y->stride*i, item_size);
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

public bool Array$equal(const array_t *x, const array_t *y, const TypeInfo *type)
{
    return (Array$compare(x, y, type) == 0);
}

public CORD Array$as_text(const array_t *arr, bool colorize, const TypeInfo *type)
{
    if (!arr)
        return CORD_all("[", generic_as_text(NULL, false, type->ArrayInfo.item), "]");

    const TypeInfo *item_type = type->ArrayInfo.item;
    CORD c = "[";
    for (int64_t i = 0; i < arr->length; i++) {
        if (i > 0)
            c = CORD_cat(c, ", ");
        CORD item_cord = generic_as_text(arr->data + i*arr->stride, colorize, item_type);
        c = CORD_cat(c, item_cord);
    }
    c = CORD_cat(c, "]");
    return c;
}

public uint32_t Array$hash(const array_t *arr, const TypeInfo *type)
{
    // Array hash is calculated as a rolling, compacting hash of the length of the array, followed by
    // the hashes of its items (or the items themselves if they're small plain data)
    // In other words, it reads in a chunk of items or item hashes, then when it fills up the chunk,
    // hashes it down to a single item to start the next chunk. This repeats until the end, when it
    // hashes the last chunk down to a uint32_t.
    const TypeInfo *item = type->ArrayInfo.item;
    if (item->tag == PointerInfo || (item->tag == CustomInfo && item->CustomInfo.hash == NULL)) { // Raw data hash
        int64_t item_size = item->size;
        uint8_t hash_batch[4 + 8*item_size];
        memset(hash_batch, 0, sizeof(hash_batch));
        uint8_t *p = hash_batch, *end = hash_batch + sizeof(hash_batch);
        int64_t length = arr->length;
        *p = (uint32_t)length;
        p += sizeof(uint32_t);
        for (int64_t i = 0; i < arr->length; i++) {
            if (p >= end) {
                uint32_t chunk_hash;
                halfsiphash(&hash_batch, sizeof(hash_batch), TOMO_HASH_VECTOR, (uint8_t*)&chunk_hash, sizeof(chunk_hash));
                p = hash_batch;
                *(uint32_t*)p = chunk_hash;
                p += sizeof(uint32_t);
            }
            memcpy((p += item_size), arr->data + i*arr->stride, item_size);
        }
        uint32_t hash;
        halfsiphash(&hash_batch, ((int64_t)p) - ((int64_t)hash_batch), TOMO_HASH_VECTOR, (uint8_t*)&hash, sizeof(hash));
        return hash;
    } else {
        uint32_t hash_batch[16] = {(uint32_t)arr->length};
        uint32_t *p = &hash_batch[1], *end = hash_batch + sizeof(hash_batch)/sizeof(hash_batch[0]);
        for (int64_t i = 0; i < arr->length; i++) {
            if (p >= end) {
                uint64_t chunk_hash;
                halfsiphash(&hash_batch, sizeof(hash_batch), TOMO_HASH_VECTOR, (uint8_t*)&chunk_hash, sizeof(chunk_hash));
                p = hash_batch;
                *(p++) = chunk_hash;
            }
            *(p++) = generic_hash(arr->data + i*arr->stride, item);
        }
        uint32_t hash;
        halfsiphash(&hash_batch, ((int64_t)p) - ((int64_t)hash_batch), TOMO_HASH_VECTOR, (uint8_t*)&hash, sizeof(hash));
        return hash;
    }
}
/*
def _siftdown(heap, startpos, pos):
    newitem = heap[pos]
    # Follow the path to the root, moving parents down until finding a place
    # newitem fits.
    while pos > startpos:
        parentpos = (pos - 1) >> 1
        parent = heap[parentpos]
        if newitem < parent:
            heap[pos] = parent
            pos = parentpos
            continue
        break
    heap[pos] = newitem
    */

static int siftdown(array_t *heap, int64_t startpos, int64_t pos, closure_t comparison, const TypeInfo *type)
{
    assert(pos > 0 && pos < heap->length);
    int64_t item_size = get_item_size(type);
    char newitem[item_size];
    memcpy(newitem, heap->data + heap->stride*pos, item_size);
    while (pos > startpos) {
        int64_t parentpos = (pos - 1) >> 1;
        typedef int32_t (*cmp_fn_t)(void*, void*, void*);
        int32_t cmp = ((cmp_fn_t)comparison.fn)(newitem, heap->data + heap->stride*parentpos, comparison.userdata);
        if (cmp >= 0)
            break;

        memcpy(newitem, heap->data + heap->stride*pos, item_size);
        // swap pos/parentpos:
        memcpy(heap->data + heap->stride*pos, heap->data + heap->stride*parentpos, item_size);
        memcpy(heap->data + heap->stride*parentpos, newitem, item_size);

        pos = parentpos;
    }
    return 0;
}

static int siftup(array_t *heap, int64_t pos, closure_t comparison, const TypeInfo *type)
{
    int64_t endpos = heap->length;
    int64_t startpos = pos;
    assert(pos < endpos);

    int64_t item_size = get_item_size(type);
    /* Bubble up the smaller child until hitting a leaf. */
    int64_t limit = endpos >> 1;         /* smallest pos that has no child */
    while (pos < limit) {
        /* Set childpos to index of smaller child.   */
        int64_t childpos = 2*pos + 1;    /* leftmost child position  */
        if (childpos + 1 < endpos) {
            typedef int32_t (*cmp_fn_t)(void*, void*, void*);
            int32_t cmp = ((cmp_fn_t)comparison.fn)(
                heap->data + heap->stride*childpos,
                heap->data + heap->stride*(childpos + 1),
                comparison.userdata);
            // if (cmp < 0)
            //     return -1;
            childpos += (cmp >= 0);
        }

        if (heap->data_refcount >= 3) {
            Array$compact(heap, type);
            heap->data_refcount = 1;
        }

        /* Move the smaller child up. */
        char buf[item_size];
        memcpy(buf, heap->data + heap->stride*pos, item_size);
        memcpy(heap->data + heap->stride*pos, heap->data + heap->stride*childpos, item_size);
        memcpy(heap->data + heap->stride*childpos, buf, item_size);

        pos = childpos;
    }
    /* Bubble it up to its final resting place (by sifting its parents down). */
    return siftdown(heap, startpos, pos, comparison, type);
}

public void Array$heap_push(array_t *heap, const void *item, closure_t comparison, const TypeInfo *type)
{
    Array$insert(heap, item, 0, type);

    if (heap->data_refcount > 0)
        Array$compact(heap, type);

    if (heap->length > 1)
        siftdown(heap, 0, heap->length-1, comparison, type);
}

public void Array$heap_pop(array_t *heap, void *out, closure_t comparison, const TypeInfo *type)
{
    if (heap->length == 0)
        fail("Attempt to pop from an empty array");

    int64_t item_size = get_item_size(type);
    memcpy(out, heap->data, item_size);
    if (heap->length > 1) {
        if (heap->data_refcount > 0)
            Array$compact(heap, type);
        memcpy(heap->data, heap->data + heap->stride*(heap->length-1), item_size);
        --heap->length;
        if (heap->length > 1)
            siftup(heap, 0, comparison, type);
    } else {
        *heap = (array_t){};
    }
}

static int64_t
keep_top_bit(int64_t n)
{
    int i = 0;
    for (; n > 1; n >>= 1)
        ++i;
    return n << i;
}

public void Array$heapify(array_t *heap, closure_t comparison, const TypeInfo *type)
{
    if (heap->data_refcount > 0)
        Array$compact(heap, type);

    ARRAY_INCREF(*heap);
    /* For heaps likely to be bigger than L1 cache, we use the cache
       friendly heapify function.  For smaller heaps that fit entirely
       in cache, we prefer the simpler algorithm with less branching.
    */
    if (heap->length <= 2500) {
        /* Transform bottom-up.  The largest index there's any point to
           looking at is the largest with a child index in-range, so must
           have 2*i + 1 < n, or i < (n-1)/2.  If n is even = 2*j, this is
           (2*j-1)/2 = j-1/2 so j-1 is the largest, which is n//2 - 1.  If
           n is odd = 2*j+1, this is (2*j+1-1)/2 = j so j-1 is the largest,
           and that's again n//2-1.
        */
        int64_t i, n = heap->length;
        for (i = (n >> 1) - 1 ; i >= 0 ; i--)
            if (siftup(heap, i, comparison, type))
                goto cleanup;
    } else {
        /* Cache friendly version of heapify()
           -----------------------------------

           Build-up a heap in O(n) time by performing siftup() operations
           on nodes whose children are already heaps.

           The simplest way is to sift the nodes in reverse order from
           n//2-1 to 0 inclusive.  The downside is that children may be
           out of cache by the time their parent is reached.

           A better way is to not wait for the children to go out of cache.
           Once a sibling pair of child nodes have been sifted, immediately
           sift their parent node (while the children are still in cache).

           Both ways build child heaps before their parents, so both ways
           do the exact same number of comparisons and produce exactly
           the same heap.  The only difference is that the traversal
           order is optimized for cache efficiency.
        */
        int64_t m = heap->length >> 1;         /* index of first childless node */
        int64_t leftmost = keep_top_bit(m + 1) - 1;     /* leftmost node in row of m */
        int64_t mhalf = m >> 1;                         /* parent of first childless node */
        int64_t i;
        for (i = leftmost - 1 ; i >= mhalf ; i--) {
            int64_t j = i;
            while (1) {
                if (siftup(heap, j, comparison, type))
                    goto cleanup;
                if (!(j & 1))
                    break;
                j >>= 1;
            }
        }

        for (i = m - 1 ; i >= leftmost ; i--) {
            int64_t j = i;
            while (1) {
                if (siftup(heap, j, comparison, type))
                    goto cleanup;
                if (!(j & 1))
                    break;
                j >>= 1;
            }
        }
    }
  cleanup:
    ARRAY_DECREF(*heap);
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
