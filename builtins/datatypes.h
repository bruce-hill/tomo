#pragma once
#include <stdint.h>
#include <stdbool.h>

#define MIN_STRIDE (INT16_MIN>>1)
#define MAX_STRIDE (INT16_MAX>>1)
typedef struct {
    void *data;
    int64_t length:42;
    uint8_t free:4, data_refcount:2;
    bool atomic:1;
    int16_t stride:15;
} array_t;

typedef struct {
    uint32_t occupied:1, index:31;
    uint32_t next_bucket;
} bucket_t;

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

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
