#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    void *data;
    int64_t length:42;
    uint8_t free:4;
    bool copy_on_write:1, atomic:1;
    int16_t stride:16;
} array_t;

typedef struct {
    uint32_t occupied:1, index:31;
    uint32_t next_bucket;
} bucket_t;

typedef struct {
    uint32_t count:32, last_free:31;
    bool copy_on_write:1;
    bucket_t buckets[0];
} bucket_info_t;

typedef struct table_s {
    array_t entries;
    bucket_info_t *bucket_info;
    struct table_s *fallback;
    void *default_value;
} table_t;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
