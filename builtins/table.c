
// table.c - C Hash table implementation for SSS
// Copyright 2023 Bruce Hill
// Provided under the MIT license with the Commons Clause
// See included LICENSE for details.

// Hash table (aka Dictionary) Implementation
// Hash keys and values are stored *by value*
// The hash insertion/lookup implementation is based on Lua's tables,
// which use a chained scatter with Brent's variation.

#include <assert.h>
#include <gc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include "../SipHash/halfsiphash.h"
#include "../util.h"
#include "array.h"
#include "datatypes.h"
#include "text.h"
#include "table.h"
#include "types.h"

// #define DEBUG_TABLES

#ifdef DEBUG_TABLES
#define hdebug(fmt, ...) printf("\x1b[2m" fmt "\x1b[m" __VA_OPT__(,) __VA_ARGS__)
#else
#define hdebug(...) (void)0
#endif

// Helper accessors for type functions/values:
#define HASH_KEY(t, k) (generic_hash((k), type->TableInfo.key) % ((t)->bucket_info->count))
#define EQUAL_KEYS(x, y) (generic_equal((x), (y), type->TableInfo.key))
#define END_OF_CHAIN UINT32_MAX

#define GET_ENTRY(t, i) ((t)->entries.data + (t)->entries.stride*(i))
#define ENTRIES_TYPE(type) (&(TypeInfo){.size=sizeof(array_t), .align=__alignof__(array_t), .tag=ArrayInfo, .ArrayInfo.item=(&(TypeInfo){.size=entry_size(type), .align=entry_align(type), .tag=OpaqueInfo})})

TypeInfo MemoryPointer = {
    .size=sizeof(void*),
    .align=__alignof__(void*),
    .tag=PointerInfo,
    .PointerInfo={
        .sigil="@",
        .pointed=NULL,
    },
};

TypeInfo StrToVoidStarTable = {
    .size=sizeof(table_t),
    .align=__alignof__(table_t),
    .tag=TableInfo,
    .TableInfo={.key=&Text, .value=&MemoryPointer},
};

static inline size_t entry_size(const TypeInfo *info)
{
    size_t size = info->TableInfo.key->size;
    if (info->TableInfo.value->align > 1 && size % info->TableInfo.value->align)
        size += info->TableInfo.value->align - (size % info->TableInfo.value->align); // padding
    size += info->TableInfo.value->size;
    if (info->TableInfo.key->align > 1 && size % info->TableInfo.key->align)
        size += info->TableInfo.key->align - (size % info->TableInfo.key->align); // padding
    return size;
}

static inline size_t entry_align(const TypeInfo *info)
{
    return MAX(info->TableInfo.key->align, info->TableInfo.value->align);
}

static inline size_t value_offset(const TypeInfo *info)
{
    size_t offset = info->TableInfo.key->size;
    if (info->TableInfo.value->align > 1 && offset % info->TableInfo.value->align)
        offset += info->TableInfo.value->align - (offset % info->TableInfo.value->align); // padding
    return offset;
}


static inline void hshow(const table_t *t)
{
    hdebug("{");
    for (uint32_t i = 0; t->bucket_info && i < t->bucket_info->count; i++) {
        if (i > 0) hdebug(" ");
        if (t->bucket_info->buckets[i].occupied)
            hdebug("[%d]=%d(%d)", i, t->bucket_info->buckets[i].index, t->bucket_info->buckets[i].next_bucket);
        else
            hdebug("[%d]=_", i);
    }
    hdebug("}\n");
}

static void maybe_copy_on_write(table_t *t, const TypeInfo *type)
{
    if (t->entries.data_refcount) {
        Array__compact(&t->entries, ENTRIES_TYPE(type));
    }

    if (t->bucket_info && t->bucket_info->data_refcount) {
        int64_t size = sizeof(bucket_info_t) + t->bucket_info->count*sizeof(bucket_t);
        t->bucket_info = memcpy(GC_MALLOC(size), t->bucket_info, size);
        t->bucket_info->data_refcount = 0;
    }
}

public void Table_mark_copy_on_write(table_t *t)
{
    t->entries.data_refcount = 3;
    if (t->bucket_info) t->bucket_info->data_refcount = 3;
}

// Return address of value or NULL
public void *Table_get_raw(const table_t *t, const void *key, const TypeInfo *type)
{
    assert(type->tag == TableInfo);
    if (!t || !key || !t->bucket_info) return NULL;

    uint32_t hash = HASH_KEY(t, key);
    hshow(t);
    hdebug("Getting value with initial probe at %u\n", hash);
    bucket_t *buckets = t->bucket_info->buckets;
    for (uint32_t i = hash; buckets[i].occupied; i = buckets[i].next_bucket) {
        hdebug("Checking against key in bucket %u\n", i);
        void *entry = GET_ENTRY(t, buckets[i].index);
        if (EQUAL_KEYS(entry, key)) {
            hdebug("Found key!\n");
            return entry + value_offset(type);
        }
        if (buckets[i].next_bucket == END_OF_CHAIN)
            break;
    }
    return NULL;
}

public void *Table_get(const table_t *t, const void *key, const TypeInfo *type)
{
    assert(type->tag == TableInfo);
    for (const table_t *iter = t; iter; iter = iter->fallback) {
        void *ret = Table_get_raw(iter, key, type);
        if (ret) return ret;
    }
    for (const table_t *iter = t; iter; iter = iter->fallback) {
        if (iter->default_value) return iter->default_value;
    }
    return NULL;
}

static void Table_set_bucket(table_t *t, const void *entry, int32_t index, const TypeInfo *type)
{
    assert(t->bucket_info);
    hshow(t);
    const void *key = entry;
    bucket_t *buckets = t->bucket_info->buckets;
    uint32_t hash = HASH_KEY(t, key);
    hdebug("Hash value (mod %u) = %u\n", t->bucket_info->count, hash);
    bucket_t *bucket = &buckets[hash];
    if (!bucket->occupied) {
        hdebug("Got an empty space\n");
        // Empty space:
        bucket->occupied = 1;
        bucket->index = index;
        bucket->next_bucket = END_OF_CHAIN;
        hshow(t);
        return;
    }

    hdebug("Collision detected in bucket %u (entry %u)\n", hash, bucket->index);

    while (buckets[t->bucket_info->last_free].occupied) {
        assert(t->bucket_info->last_free > 0);
        --t->bucket_info->last_free;
    }

    uint32_t collided_hash = HASH_KEY(t, GET_ENTRY(t, bucket->index));
    if (collided_hash != hash) { // Collided with a mid-chain entry
        hdebug("Hit a mid-chain entry at bucket %u (chain starting at %u)\n", hash, collided_hash);
        // Find chain predecessor
        uint32_t predecessor = collided_hash;
        while (buckets[predecessor].next_bucket != hash)
            predecessor = buckets[predecessor].next_bucket;

        // Move mid-chain entry to free space and update predecessor
        buckets[predecessor].next_bucket = t->bucket_info->last_free;
        buckets[t->bucket_info->last_free] = *bucket;
    } else { // Collided with the start of a chain
        hdebug("Hit start of a chain\n");
        uint32_t end_of_chain = hash;
        while (buckets[end_of_chain].next_bucket != END_OF_CHAIN)
            end_of_chain = buckets[end_of_chain].next_bucket;
        hdebug("Appending to chain\n");
        // Chain now ends on the free space:
        buckets[end_of_chain].next_bucket = t->bucket_info->last_free;
        bucket = &buckets[t->bucket_info->last_free];
    }

    bucket->occupied = 1;
    bucket->index = index;
    bucket->next_bucket = END_OF_CHAIN;
    hshow(t);
}

static void hashmap_resize_buckets(table_t *t, uint32_t new_capacity, const TypeInfo *type)
{
    hdebug("About to resize from %u to %u\n", t->bucket_info ? t->bucket_info->count : 0, new_capacity);
    hshow(t);
    int64_t alloc_size = sizeof(bucket_info_t) + (int64_t)(new_capacity)*sizeof(bucket_t);
    t->bucket_info = GC_MALLOC_ATOMIC(alloc_size);
    memset(t->bucket_info->buckets, 0, (int64_t)new_capacity * sizeof(bucket_t));
    t->bucket_info->count = new_capacity;
    t->bucket_info->last_free = new_capacity-1;
    // Rehash:
    for (int64_t i = 0; i < Table_length(t); i++) {
        hdebug("Rehashing %u\n", i);
        Table_set_bucket(t, GET_ENTRY(t, i), i, type);
    }

    hshow(t);
    hdebug("Finished resizing\n");
}

// Return address of value
public void *Table_reserve(table_t *t, const void *key, const void *value, const TypeInfo *type)
{
    assert(type->tag == TableInfo);
    if (!t || !key) return NULL;
    hshow(t);

    int64_t key_size = type->TableInfo.key->size,
            value_size = type->TableInfo.value->size;
    if (!t->bucket_info || t->bucket_info->count == 0) {
        hashmap_resize_buckets(t, 4, type);
    } else {
        // Check if we are clobbering a value:
        void *value_home = Table_get_raw(t, key, type);
        if (value_home) { // Update existing slot
            // Ensure that `value_home` is still inside t->entries, even if COW occurs
            ptrdiff_t offset = value_home - t->entries.data;
            maybe_copy_on_write(t, type);
            value_home = t->entries.data + offset;

            if (value && value_size > 0)
                memcpy(value_home, value, value_size);

            return value_home;
        }
    }
    // Otherwise add a new entry:

    // Resize buckets if necessary
    if (t->entries.length >= (int64_t)t->bucket_info->count) {
        uint32_t newsize = t->bucket_info->count + MIN(t->bucket_info->count, 64);
        hashmap_resize_buckets(t, newsize, type);
    }

    if (!value && value_size > 0) {
        for (table_t *iter = t->fallback; iter; iter = iter->fallback) {
            value = Table_get_raw(iter, key, type);
            if (value) break;
        }
        for (table_t *iter = t; !value && iter; iter = iter->fallback) {
            if (iter->default_value) value = iter->default_value;
        }
    }

    maybe_copy_on_write(t, type);

    char buf[entry_size(type)] = {};
    memcpy(buf, key, key_size);
    if (value && value_size > 0)
        memcpy(buf + value_offset(type), value, value_size);
    else
        memset(buf + value_offset(type), 0, value_size);
    Array__insert(&t->entries, buf, 0, ENTRIES_TYPE(type));

    int64_t entry_index = t->entries.length-1;
    void *entry = GET_ENTRY(t, entry_index);
    Table_set_bucket(t, entry, entry_index, type);
    return entry + value_offset(type);
}

public void Table_set(table_t *t, const void *key, const void *value, const TypeInfo *type)
{
    assert(type->tag == TableInfo);
    (void)Table_reserve(t, key, value, type);
}

public void Table_remove(table_t *t, const void *key, const TypeInfo *type)
{
    assert(type->tag == TableInfo);
    if (!t || Table_length(t) == 0) return;

    // TODO: this work doesn't need to be done if the key is already missing
    maybe_copy_on_write(t, type);

    // If unspecified, pop a random key:
    if (!key) {
        hdebug("Popping random key\n");
        uint32_t index = arc4random_uniform(t->entries.length);
        key = GET_ENTRY(t, index);
    }

    // Steps: look up the bucket for the removed key
    // If missing, then return immediately
    // Swap last key/value into the removed bucket's index1
    // Zero out the last key/value and decrement the count
    // Find the last key/value's bucket and update its index1
    // Look up the bucket for the removed key
    // If bucket is first in chain:
    //    Move bucket->next to bucket's spot
    //    zero out bucket->next's old spot
    //    maybe update lastfree_index1 to second-in-chain's index
    // Else:
    //    set prev->next = bucket->next
    //    zero out bucket
    //    maybe update lastfree_index1 to removed bucket's index

    uint32_t hash = HASH_KEY(t, key);
    hdebug("Removing key with hash %u\n", hash);
    bucket_t *bucket, *prev = NULL;
    for (uint32_t i = hash; t->bucket_info->buckets[i].occupied; i = t->bucket_info->buckets[i].next_bucket) {
        if (EQUAL_KEYS(GET_ENTRY(t, t->bucket_info->buckets[i].index), key)) {
            bucket = &t->bucket_info->buckets[i];
            hdebug("Found key to delete in bucket %u\n", i);
            goto found_it;
        }
        if (t->bucket_info->buckets[i].next_bucket == END_OF_CHAIN)
            return;
        prev = &t->bucket_info->buckets[i];
    }
    return;

  found_it:;
    assert(bucket->occupied);

    // Always remove the last entry. If we need to remove some other entry,
    // swap the other entry into the last position and then remove the last
    // entry. This disturbs the ordering of the table, but keeps removal O(1)
    // instead of O(N)
    int64_t last_entry = t->entries.length-1;
    if (bucket->index != last_entry) {
        hdebug("Removing key/value from the middle of the entries array\n");

        // Find the bucket that points to the last entry's index:
        uint32_t i = HASH_KEY(t, GET_ENTRY(t, last_entry));
        while (t->bucket_info->buckets[i].index != last_entry)
            i = t->bucket_info->buckets[i].next_bucket;
        // Update the bucket to point to the last entry's new home (the space
        // where the removed entry currently sits):
        t->bucket_info->buckets[i].index = bucket->index;

        // Clobber the entry being removed (in the middle of the array) with
        // the last entry:
        memcpy(GET_ENTRY(t, bucket->index), GET_ENTRY(t, last_entry), entry_size(type));
    }

    // Last entry is being removed, so clear it out to be safe:
    memset(GET_ENTRY(t, last_entry), 0, entry_size(type));

    Array__remove(&t->entries, t->entries.length, 1, ENTRIES_TYPE(type));

    int64_t bucket_to_clear;
    if (prev) { // Middle (or end) of a chain
        hdebug("Removing from middle of a chain\n");
        bucket_to_clear = (bucket - t->bucket_info->buckets);
        prev->next_bucket = bucket->next_bucket;
    } else if (bucket->next_bucket != END_OF_CHAIN) { // Start of a chain
        hdebug("Removing from start of a chain\n");
        bucket_to_clear = bucket->next_bucket;
        *bucket = t->bucket_info->buckets[bucket_to_clear];
    } else { // Empty chain
        hdebug("Removing from empty chain\n");
        bucket_to_clear = (bucket - t->bucket_info->buckets);
    }

    t->bucket_info->buckets[bucket_to_clear] = (bucket_t){0};
    if (bucket_to_clear > t->bucket_info->last_free)
        t->bucket_info->last_free = bucket_to_clear;

    hshow(t);
}

public void *Table_entry(const table_t *t, int64_t n)
{
    if (n < 1 || n > Table_length(t))
        return NULL;
    return GET_ENTRY(t, n-1);
}

public void Table_clear(table_t *t)
{
    memset(t, 0, sizeof(table_t));
}

public bool Table_equal(const table_t *x, const table_t *y, const TypeInfo *type)
{
    assert(type->tag == TableInfo);
    if (Table_length(x) != Table_length(y))
        return false;
    
    if ((x->default_value != NULL) != (y->default_value != NULL))
        return false;
    
    if ((x->fallback != NULL) != (y->fallback != NULL))
        return false;

    const TypeInfo *value_type = type->TableInfo.value;
    for (int64_t i = 0, length = Table_length(x); i < length; i++) {
        void *x_key = GET_ENTRY(x, i);
        void *x_value = x_key + value_offset(type);
        void *y_value = Table_get_raw(y, x_key, type);
        if (!y_value)
            return false;
        if (!generic_equal(x_value, y_value, value_type))
            return false;
    }

    if (x->default_value && y->default_value
        && !generic_equal(x->default_value, y->default_value, value_type))
        return false;

    if (x->fallback && y->fallback
        && !Table_equal(x->fallback, y->fallback, type))
        return false;
    
    return true;
}

public int32_t Table_compare(const table_t *x, const table_t *y, const TypeInfo *type)
{
    assert(type->tag == TableInfo);
    auto table = type->TableInfo;
    if (x->entries.length == 0)
        return 0;
    else if (x->entries.length != y->entries.length)
        return (x->entries.length > y->entries.length) - (x->entries.length < y->entries.length);

    array_t x_entries = x->entries, y_entries = y->entries;
    Array__sort(&x_entries, table.key);
    Array__sort(&y_entries, table.key);
    for (int64_t i = 0; i < x_entries.length; i++) {
        void *x_key = x_entries.data + x_entries.stride * i;
        void *y_key = y_entries.data + y_entries.stride * i;
        int32_t diff = generic_compare(x_key, y_key, table.key);
        if (diff != 0) return diff;
        void *x_value = x_key + value_offset(type);
        void *y_value = y_key + value_offset(type);
        diff = generic_compare(x_value, y_value, table.value);
        if (diff != 0) return diff;
    }

    if (!x->default_value != !y->default_value) {
        return (!x->default_value) - (!y->default_value);
    } else if (x->default_value && y->default_value) {
        int32_t diff = generic_compare(x->default_value, y->default_value, table.value);
        if (diff != 0) return diff;
    }

    if (!x->fallback != !y->fallback) {
        return (!x->fallback) - (!y->fallback);
    } else if (x->fallback && y->fallback) {
        return generic_compare(x->fallback, y->fallback, type);
    }

    return 0;
}

public uint32_t Table_hash(const table_t *t, const TypeInfo *type)
{
    assert(type->tag == TableInfo);
    // Table hashes are computed as:
    // hash(#t, xor(hash(k) for k in t.keys), xor(hash(v) for v in t.values), hash(t.fallback), hash(t.default))
    // Where fallback and default hash to zero if absent
    auto table = type->TableInfo;
    int64_t val_off = value_offset(type);

    uint32_t key_hashes = 0, value_hashes = 0, fallback_hash = 0, default_hash = 0;
    for (int64_t i = 0, length = Table_length(t); i < length; i++) {
        void *entry = GET_ENTRY(t, i);
        key_hashes ^= generic_hash(entry, table.key);
        value_hashes ^= generic_hash(entry + val_off, table.value);
    }

    if (t->fallback)
        fallback_hash = Table_hash(t->fallback, type);

    if (t->default_value)
        default_hash = generic_hash(t->default_value, table.value);

    struct { int64_t len; uint32_t k, v, f, d; } components = {
        Table_length(t),
        key_hashes,
        value_hashes,
        fallback_hash,
        default_hash,
    };
    uint32_t hash;
    halfsiphash(&components, sizeof(components), SSS_HASH_VECTOR, (uint8_t*)&hash, sizeof(hash));
    return hash;
}

public CORD Table_as_str(const table_t *t, bool colorize, const TypeInfo *type)
{
    assert(type->tag == TableInfo);
    auto table = type->TableInfo;

    if (!t)
        return CORD_all("{", generic_as_str(NULL, false, table.key), "=>", generic_as_str(NULL, false, table.value), "}");

    int64_t val_off = value_offset(type);
    CORD c = "{";
    for (int64_t i = 0, length = Table_length(t); i < length; i++) {
        if (i > 0)
            c = CORD_cat(c, ", ");
        void *entry = GET_ENTRY(t, i);
        c = CORD_cat(c, generic_as_str(entry, colorize, table.key));
        c = CORD_cat(c, "=>");
        c = CORD_cat(c, generic_as_str(entry + val_off, colorize, table.value));
    }

    if (t->fallback) {
        c = CORD_cat(c, "; fallback=");
        c = CORD_cat(c, Table_as_str(t->fallback, colorize, type));
    }

    if (t->default_value) {
        c = CORD_cat(c, "; default=");
        c = CORD_cat(c, generic_as_str(t->default_value, colorize, table.value));
    }

    c = CORD_cat(c, "}");
    return c;
}

public table_t Table_from_entries(array_t entries, const TypeInfo *type)
{
    assert(type->tag == TableInfo);
    table_t t = {.entries=entries};
    hashmap_resize_buckets(&t, entries.length, type);
    return t;
}

void *Table_str_get(const table_t *t, const char *key)
{
    void **ret = Table_get(t, &key, &StrToVoidStarTable);
    return ret ? *ret : NULL;
}

void *Table_str_get_raw(const table_t *t, const char *key)
{
    void **ret = Table_get_raw(t, &key, &StrToVoidStarTable);
    return ret ? *ret : NULL;
}

void *Table_str_reserve(table_t *t, const char *key, const void *value)
{
    return Table_reserve(t, &key, &value, &StrToVoidStarTable);
}

void Table_str_set(table_t *t, const char *key, const void *value)
{
    Table_set(t, &key, &value, &StrToVoidStarTable);
}

void Table_str_remove(table_t *t, const char *key)
{
    return Table_remove(t, &key, &StrToVoidStarTable);
}

void *Table_str_entry(const table_t *t, int64_t n)
{
    return Table_entry(t, n);
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1
