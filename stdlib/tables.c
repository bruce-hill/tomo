// table.c - C Hash table implementation
// Copyright 2024 Bruce Hill
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

#include "arrays.h"
#include "c_strings.h"
#include "datatypes.h"
#include "memory.h"
#include "metamethods.h"
#include "pointers.h"
#include "siphash.h"
#include "tables.h"
#include "text.h"
#include "types.h"
#include "util.h"

// #define DEBUG_TABLES

#ifdef DEBUG_TABLES
#define hdebug(fmt, ...) printf("\x1b[2m" fmt "\x1b[m" __VA_OPT__(,) __VA_ARGS__)
#else
#define hdebug(...) (void)0
#endif

// Helper accessors for type functions/values:
#define HASH_KEY(t, k) (generic_hash((k), type->TableInfo.key) % ((t).bucket_info->count))
#define EQUAL_KEYS(x, y) (generic_equal((x), (y), type->TableInfo.key))
#define END_OF_CHAIN UINT32_MAX

#define GET_ENTRY(t, i) ((t).entries.data + (t).entries.stride*(i))

static TypeInfo_t MemoryPointer = {
    .size=sizeof(void*),
    .align=__alignof__(void*),
    .tag=PointerInfo,
    .PointerInfo={
        .sigil="@",
        .pointed=&Memory$info,
    },
    .metamethods=Pointer$metamethods,
};

const TypeInfo_t CStrToVoidStarTable = {
    .size=sizeof(Table_t),
    .align=__alignof__(Table_t),
    .tag=TableInfo,
    .TableInfo={.key=&CString$info, .value=&MemoryPointer},
    .metamethods=Table$metamethods,
};

PUREFUNC static INLINE size_t entry_size(const TypeInfo_t *info)
{
    size_t size = (size_t)info->TableInfo.key->size;
    if (info->TableInfo.value->align > 1 && size % (size_t)info->TableInfo.value->align)
        size += (size_t)info->TableInfo.value->align - (size % (size_t)info->TableInfo.value->align); // padding
    size += (size_t)info->TableInfo.value->size;
    if (info->TableInfo.key->align > 1 && size % (size_t)info->TableInfo.key->align)
        size += (size_t)info->TableInfo.key->align - (size % (size_t)info->TableInfo.key->align); // padding
    return size;
}

PUREFUNC static INLINE size_t entry_align(const TypeInfo_t *info)
{
    return (size_t)MAX(info->TableInfo.key->align, info->TableInfo.value->align);
}

PUREFUNC static INLINE size_t value_offset(const TypeInfo_t *info)
{
    size_t offset = (size_t)info->TableInfo.key->size;
    if ((size_t)info->TableInfo.value->align > 1 && offset % (size_t)info->TableInfo.value->align)
        offset += (size_t)info->TableInfo.value->align - (offset % (size_t)info->TableInfo.value->align); // padding
    return offset;
}

static INLINE void hshow(const Table_t *t)
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

static void maybe_copy_on_write(Table_t *t, const TypeInfo_t *type)
{
    if (t->entries.data_refcount != 0)
        Array$compact(&t->entries, (int64_t)entry_size(type));

    if (t->bucket_info && t->bucket_info->data_refcount != 0) {
        size_t size = sizeof(bucket_info_t) + sizeof(bucket_t[t->bucket_info->count]);
        t->bucket_info = memcpy(GC_MALLOC(size), t->bucket_info, size);
        t->bucket_info->data_refcount = 0;
    }
}

// Return address of value or NULL
PUREFUNC public void *Table$get_raw(Table_t t, const void *key, const TypeInfo_t *type)
{
    assert(type->tag == TableInfo);
    if (!key || !t.bucket_info) return NULL;

    uint64_t hash = HASH_KEY(t, key);
    hshow(&t);
    hdebug("Getting value with initial probe at %u\n", hash);
    bucket_t *buckets = t.bucket_info->buckets;
    for (uint64_t i = hash; buckets[i].occupied; i = buckets[i].next_bucket) {
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

PUREFUNC public void *Table$get(Table_t t, const void *key, const TypeInfo_t *type)
{
    assert(type->tag == TableInfo);
    for (const Table_t *iter = &t; iter; iter = iter->fallback) {
        void *ret = Table$get_raw(*iter, key, type);
        if (ret) return ret;
    }
    return NULL;
}

static void Table$set_bucket(Table_t *t, const void *entry, int32_t index, const TypeInfo_t *type)
{
    assert(t->bucket_info);
    hshow(t);
    const void *key = entry;
    bucket_t *buckets = t->bucket_info->buckets;
    uint64_t hash = HASH_KEY(*t, key);
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

    uint64_t collided_hash = HASH_KEY(*t, GET_ENTRY(*t, bucket->index));
    if (collided_hash != hash) { // Collided with a mid-chain entry
        hdebug("Hit a mid-chain entry at bucket %u (chain starting at %u)\n", hash, collided_hash);
        // Find chain predecessor
        uint64_t predecessor = collided_hash;
        while (buckets[predecessor].next_bucket != hash)
            predecessor = buckets[predecessor].next_bucket;

        // Move mid-chain entry to free space and update predecessor
        buckets[predecessor].next_bucket = t->bucket_info->last_free;
        buckets[t->bucket_info->last_free] = *bucket;
    } else { // Collided with the start of a chain
        hdebug("Hit start of a chain\n");
        uint64_t end_of_chain = hash;
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

static void hashmap_resize_buckets(Table_t *t, uint32_t new_capacity, const TypeInfo_t *type)
{
    if (unlikely(new_capacity > TABLE_MAX_BUCKETS))
        fail("Table has exceeded the maximum table size (2^31) and cannot grow further!");
    hdebug("About to resize from %u to %u\n", t->bucket_info ? t->bucket_info->count : 0, new_capacity);
    hshow(t);
    size_t alloc_size = sizeof(bucket_info_t) + sizeof(bucket_t[new_capacity]);
    t->bucket_info = GC_MALLOC_ATOMIC(alloc_size);
    memset(t->bucket_info->buckets, 0, sizeof(bucket_t[new_capacity]));
    t->bucket_info->count = new_capacity;
    t->bucket_info->last_free = new_capacity-1;
    // Rehash:
    for (int64_t i = 0; i < Table$length(*t); i++) {
        hdebug("Rehashing %u\n", i);
        Table$set_bucket(t, GET_ENTRY(*t, i), i, type);
    }

    hshow(t);
    hdebug("Finished resizing\n");
}

// Return address of value
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstack-protector"
public void *Table$reserve(Table_t *t, const void *key, const void *value, const TypeInfo_t *type)
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
        void *value_home = Table$get_raw(*t, key, type);
        if (value_home) { // Update existing slot
            // Ensure that `value_home` is still inside t->entries, even if COW occurs
            ptrdiff_t offset = value_home - t->entries.data;
            maybe_copy_on_write(t, type);
            value_home = t->entries.data + offset;

            if (value && value_size > 0)
                memcpy(value_home, value, (size_t)value_size);

            return value_home;
        }
    }
    // Otherwise add a new entry:

    // Resize buckets if necessary
    if (t->entries.length >= (int64_t)t->bucket_info->count) {
        uint32_t newsize = (uint32_t)t->bucket_info->count + MIN((uint32_t)t->bucket_info->count, 64);
        if (unlikely(newsize > TABLE_MAX_BUCKETS))
            newsize = t->entries.length + 1;
        hashmap_resize_buckets(t, newsize, type);
    }

    if (!value && value_size > 0) {
        for (Table_t *iter = t->fallback; iter; iter = iter->fallback) {
            value = Table$get_raw(*iter, key, type);
            if (value) break;
        }
    }

    maybe_copy_on_write(t, type);

    char buf[entry_size(type)];
    memset(buf, 0, sizeof(buf));
    memcpy(buf, key, (size_t)key_size);
    if (value && value_size > 0)
        memcpy(buf + value_offset(type), value, (size_t)value_size);
    else
        memset(buf + value_offset(type), 0, (size_t)value_size);
    Array$insert(&t->entries, buf, I(0), (int64_t)entry_size(type));

    int64_t entry_index = t->entries.length-1;
    void *entry = GET_ENTRY(*t, entry_index);
    Table$set_bucket(t, entry, entry_index, type);
    return entry + value_offset(type);
}
#pragma GCC diagnostic pop

public void Table$set(Table_t *t, const void *key, const void *value, const TypeInfo_t *type)
{
    assert(type->tag == TableInfo);
    (void)Table$reserve(t, key, value, type);
}

public void Table$remove(Table_t *t, const void *key, const TypeInfo_t *type)
{
    assert(type->tag == TableInfo);
    if (!t || Table$length(*t) == 0) return;

    // TODO: this work doesn't need to be done if the key is already missing
    maybe_copy_on_write(t, type);

    // If unspecified, pop the last key:
    if (!key)
        key = GET_ENTRY(*t, t->entries.length-1);

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

    uint64_t hash = HASH_KEY(*t, key);
    hdebug("Removing key with hash %u\n", hash);
    bucket_t *bucket, *prev = NULL;
    for (uint64_t i = hash; t->bucket_info->buckets[i].occupied; i = t->bucket_info->buckets[i].next_bucket) {
        if (EQUAL_KEYS(GET_ENTRY(*t, t->bucket_info->buckets[i].index), key)) {
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
        uint64_t i = HASH_KEY(*t, GET_ENTRY(*t, last_entry));
        while (t->bucket_info->buckets[i].index != last_entry)
            i = t->bucket_info->buckets[i].next_bucket;
        // Update the bucket to point to the last entry's new home (the space
        // where the removed entry currently sits):
        t->bucket_info->buckets[i].index = bucket->index;

        // Clobber the entry being removed (in the middle of the array) with
        // the last entry:
        memcpy(GET_ENTRY(*t, bucket->index), GET_ENTRY(*t, last_entry), entry_size(type));
    }

    // Last entry is being removed, so clear it out to be safe:
    memset(GET_ENTRY(*t, last_entry), 0, entry_size(type));

    Array$remove_at(&t->entries, I(t->entries.length), I(1), (int64_t)entry_size(type));

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

CONSTFUNC public void *Table$entry(Table_t t, int64_t n)
{
    if (n < 1 || n > Table$length(t))
        return NULL;
    return GET_ENTRY(t, n-1);
}

public void Table$clear(Table_t *t)
{
    memset(t, 0, sizeof(Table_t));
}

public Table_t Table$sorted(Table_t t, const TypeInfo_t *type)
{
    Closure_t cmp = (Closure_t){.fn=generic_compare, .userdata=(void*)type->TableInfo.key};
    Array_t entries = Array$sorted(t.entries, cmp, (int64_t)entry_size(type));
    return Table$from_entries(entries, type);
}

PUREFUNC public bool Table$equal(const void *vx, const void *vy, const TypeInfo_t *type)
{
    if (vx == vy) return true;
    Table_t *x = (Table_t*)vx, *y = (Table_t*)vy;

    assert(type->tag == TableInfo);
    if (Table$length(*x) != Table$length(*y))
        return false;
    
    if ((x->fallback != NULL) != (y->fallback != NULL))
        return false;

    return (Table$compare(x, y, type) == 0);
}

PUREFUNC public int32_t Table$compare(const void *vx, const void *vy, const TypeInfo_t *type)
{
    if (vx == vy) return 0;

    Table_t *x = (Table_t*)vx, *y = (Table_t*)vy;
    assert(type->tag == TableInfo);
    auto table = type->TableInfo;
    if (x->entries.length == 0)
        return 0;
    else if (x->entries.length != y->entries.length)
        return (x->entries.length > y->entries.length) - (x->entries.length < y->entries.length);

    for (int64_t i = 0; i < x->entries.length; i++) {
        void *x_key = x->entries.data + x->entries.stride * i;
        void *y_key = y->entries.data + y->entries.stride * i;
        int32_t diff = generic_compare(x_key, y_key, table.key);
        if (diff != 0) return diff;
        void *x_value = x_key + value_offset(type);
        void *y_value = y_key + value_offset(type);
        diff = generic_compare(x_value, y_value, table.value);
        if (diff != 0) return diff;
    }

    if (!x->fallback != !y->fallback) {
        return (!x->fallback) - (!y->fallback);
    } else if (x->fallback && y->fallback) {
        return generic_compare(x->fallback, y->fallback, type);
    }

    return 0;
}

PUREFUNC public uint64_t Table$hash(const void *obj, const TypeInfo_t *type)
{
    assert(type->tag == TableInfo);
    Table_t *t = (Table_t*)obj;
    // Table hashes are computed as:
    // hash(hash(t.keys), hash(t.values), hash(t.fallback), hash(t.default))
    // Where fallback and default hash to zero if absent
    auto table = type->TableInfo;
    uint64_t components[] = {
        Array$hash(&t->entries, Array$info(table.key)),
        Array$hash(&t->entries + value_offset(type), Array$info(table.value)),
        t->fallback ? Table$hash(t->fallback, type) : 0,
    };
    return siphash24((void*)&components, sizeof(components));
}

public Text_t Table$as_text(const void *obj, bool colorize, const TypeInfo_t *type)
{
    Table_t *t = (Table_t*)obj;
    assert(type->tag == TableInfo);
    auto table = type->TableInfo;

    if (!t) {
        if (table.value != &Void$info) 
            return Text$concat(
                Text("{"),
                generic_as_text(NULL, false, table.key),
                Text(":"),
                generic_as_text(NULL, false, table.value),
                Text("}"));
        else
            return Text$concat(
                Text("{"),
                generic_as_text(NULL, false, table.key),
                Text("}"));
    }

    int64_t val_off = (int64_t)value_offset(type);
    Text_t text = Text("{");
    for (int64_t i = 0, length = Table$length(*t); i < length; i++) {
        if (i > 0)
            text = Text$concat(text, Text(", "));
        void *entry = GET_ENTRY(*t, i);
        text = Text$concat(text, generic_as_text(entry, colorize, table.key));
        if (table.value != &Void$info) 
            text = Text$concat(text, Text(":"), generic_as_text(entry + val_off, colorize, table.value));
    }

    if (t->fallback) {
        text = Text$concat(text, Text("; fallback="), Table$as_text(t->fallback, colorize, type));
    }

    text = Text$concat(text, Text("}"));
    return text;
}

public Table_t Table$from_entries(Array_t entries, const TypeInfo_t *type)
{
    assert(type->tag == TableInfo);
    if (entries.length == 0)
        return (Table_t){};

    Table_t t = {};
    int64_t length = entries.length + entries.length / 4;
    size_t alloc_size = sizeof(bucket_info_t) + sizeof(bucket_t[length]);
    t.bucket_info = GC_MALLOC_ATOMIC(alloc_size);
    memset(t.bucket_info->buckets, 0, sizeof(bucket_t[length]));
    t.bucket_info->count = length;
    t.bucket_info->last_free = length-1;

    size_t offset = value_offset(type);
    for (int64_t i = 0; i < entries.length; i++) {
        void *key = entries.data + i*entries.stride;
        Table$set(&t, key, key + offset, type);
    }
    return t;
}

// Overlap is "set intersection" in formal terms
public Table_t Table$overlap(Table_t a, Table_t b, const TypeInfo_t *type)
{
    // Return a table such that t[k]==a[k] for all k such that a:has(k), b:has(k), and a[k]==b[k]
    Table_t result = {};
    const size_t offset = value_offset(type);
    for (int64_t i = 0; i < Table$length(a); i++) {
        void *key = GET_ENTRY(a, i);
        void *a_value = key + offset;
        void *b_value = Table$get(b, key, type);
        if (b_value && generic_equal(a_value, b_value, type->TableInfo.value))
            Table$set(&result, key, a_value, type);
    }

    if (a.fallback) {
        result.fallback = new(Table_t);
        *result.fallback = Table$overlap(*a.fallback, b, type);
    }

    return result;
}

// With is "set union" in formal terms
public Table_t Table$with(Table_t a, Table_t b, const TypeInfo_t *type)
{
    // return a table such that t[k]==b[k] for all k such that b:has(k), and t[k]==a[k] for all k such that a:has(k) and not b:has(k)
    Table_t result = {};
    const size_t offset = value_offset(type);
    for (int64_t i = 0; i < Table$length(a); i++) {
        void *key = GET_ENTRY(a, i);
        Table$set(&result, key, key + offset, type);
    }
    for (int64_t i = 0; i < Table$length(b); i++) {
        void *key = GET_ENTRY(b, i);
        Table$set(&result, key, key + offset, type);
    }

    if (a.fallback && b.fallback) {
        result.fallback = new(Table_t);
        *result.fallback = Table$with(*a.fallback, *b.fallback, type);
    } else {
        result.fallback = a.fallback ? a.fallback : b.fallback;
    }

    return result;
}

// Without is "set difference" in formal terms
public Table_t Table$without(Table_t a, Table_t b, const TypeInfo_t *type)
{
    // Return a table such that t[k]==a[k] for all k such that not b:has(k) or b[k] != a[k]
    Table_t result = {};
    const size_t offset = value_offset(type);
    for (int64_t i = 0; i < Table$length(a); i++) {
        void *key = GET_ENTRY(a, i);
        void *a_value = key + offset;
        void *b_value = Table$get(b, key, type);
        if (!b_value || !generic_equal(a_value, b_value, type->TableInfo.value))
            Table$set(&result, key, a_value, type);
    }

    if (a.fallback) {
        result.fallback = new(Table_t);
        *result.fallback = Table$without(*a.fallback, b, type);
    }

    return result;
}

PUREFUNC public bool Table$is_subset_of(Table_t a, Table_t b, bool strict, const TypeInfo_t *type)
{
    if (a.entries.length > b.entries.length || (strict && a.entries.length == b.entries.length))
        return false;

    for (int64_t i = 0; i < Table$length(a); i++) {
        void *found = Table$get_raw(b, GET_ENTRY(a, i), type);
        if (!found) return false;
    }
    return true;
}

PUREFUNC public bool Table$is_superset_of(Table_t a, Table_t b, bool strict, const TypeInfo_t *type)
{
    return Table$is_subset_of(b, a, strict, type);
}

PUREFUNC public void *Table$str_get(Table_t t, const char *key)
{
    void **ret = Table$get(t, &key, &CStrToVoidStarTable);
    return ret ? *ret : NULL;
}

PUREFUNC public void *Table$str_get_raw(Table_t t, const char *key)
{
    void **ret = Table$get_raw(t, &key, &CStrToVoidStarTable);
    return ret ? *ret : NULL;
}

public void *Table$str_reserve(Table_t *t, const char *key, const void *value)
{
    return Table$reserve(t, &key, &value, &CStrToVoidStarTable);
}

public void Table$str_set(Table_t *t, const char *key, const void *value)
{
    Table$set(t, &key, &value, &CStrToVoidStarTable);
}

public void Table$str_remove(Table_t *t, const char *key)
{
    return Table$remove(t, &key, &CStrToVoidStarTable);
}

CONSTFUNC public void *Table$str_entry(Table_t t, int64_t n)
{
    return Table$entry(t, n);
}

PUREFUNC public bool Table$is_none(const void *obj, const TypeInfo_t*)
{
    return ((Table_t*)obj)->entries.length < 0;
}

public void Table$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type)
{
    Table_t *t = (Table_t*)obj;
    int64_t len = t->entries.length;
    Int64$serialize(&len, out, pointers, &Int64$info);

    size_t offset = value_offset(type);
    for (int64_t i = 0; i < len; i++) {
        _serialize(t->entries.data + i*t->entries.stride, out, pointers, type->TableInfo.key);
        _serialize(t->entries.data + i*t->entries.stride + offset, out, pointers, type->TableInfo.value);
    }

    Optional$serialize(&t->fallback, out, pointers, Optional$info(sizeof(void*), __alignof__(void*), Pointer$info("&", type)));
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstack-protector"
public void Table$deserialize(FILE *in, void *outval, Array_t *pointers, const TypeInfo_t *type)
{
    int64_t len;
    Int64$deserialize(in, &len, pointers, &Int$info);

    Table_t t = {};
    for (int64_t i = 0; i < len; i++) {
        char key[type->TableInfo.key->size];
        _deserialize(in, key, pointers, type->TableInfo.key);
        char value[type->TableInfo.value->size];
        _deserialize(in, value, pointers, type->TableInfo.value);
        Table$set(&t, key, value, type);
    }

    Optional$deserialize(in, &t.fallback, pointers, Optional$info(sizeof(void*), __alignof__(void*), Pointer$info("&", type)));

    *(Table_t*)outval = t;
}
#pragma GCC diagnostic pop

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1
