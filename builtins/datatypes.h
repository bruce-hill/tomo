#pragma once

// Common datastructures (arrays, tables, closures)

#include <stdint.h>
#include <stdbool.h>

#define ARRAY_LENGTH_BITS 42
#define ARRAY_FREE_BITS 6
#define ARRAY_REFCOUNT_BITS 3
#define ARRAY_STRIDE_BITS 12

#define MAX_FOR_N_BITS(N) ((1<<(N))-1)
#define ARRAY_MAX_STRIDE MAX_FOR_N_BITS(ARRAY_STRIDE_BITS-1)
#define ARRAY_MIN_STRIDE (~MAX_FOR_N_BITS(ARRAY_STRIDE_BITS-1))
#define ARRAY_MAX_DATA_REFCOUNT MAX_FOR_N_BITS(ARRAY_REFCOUNT_BITS)
#define ARRAY_MAX_FREE_ENTRIES MAX_FOR_N_BITS(ARRAY_FREE_BITS)

typedef struct {
    void *data;
    // All of the following fields add up to 64 bits, which means that array
    // structs can be passed in two 64-bit registers. C will handle doing the
    // bit arithmetic to extract the necessary values, which is cheaper than
    // spilling onto the stack and needing to retrieve data from the stack.
    int64_t length:ARRAY_LENGTH_BITS;
    uint8_t free:ARRAY_FREE_BITS;
    bool atomic:1;
    uint8_t data_refcount:ARRAY_REFCOUNT_BITS;
    int16_t stride:ARRAY_STRIDE_BITS;
} array_t;

typedef struct {
    uint32_t occupied:1, index:31;
    uint32_t next_bucket;
} bucket_t;

#define TABLE_MAX_BUCKETS 0x7fffffff
#define TABLE_MAX_DATA_REFCOUNT 3

typedef struct {
    uint32_t count:31, last_free:31;
    uint8_t data_refcount:2;
    bucket_t buckets[0];
} bucket_info_t;

typedef struct table_s {
    array_t entries;
    bucket_info_t *bucket_info;
    struct table_s *fallback;
    void *default_value;
} table_t;

typedef struct {
    void *fn, *userdata;
} closure_t;

typedef struct Range_s {
    int64_t first, last, step;
} Range_t;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
