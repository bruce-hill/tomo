// Representation of the Table type

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "list.h"

typedef struct {
    uint32_t occupied : 1, index : 31;
    uint32_t next_bucket;
} bucket_t;

// Maximum bucket size is determined by the maximum value for `index` in the `bucket_t` struct
#define TABLE_MAX_BUCKETS 0x7fffffff
#define TABLE_MAX_DATA_REFCOUNT 3

typedef struct {
    uint32_t count : 31, last_free : 31;
    uint8_t data_refcount : 2;
    bucket_t buckets[];
} bucket_info_t;

typedef struct table_s {
    List_t entries;
    uint64_t hash;
    bucket_info_t *bucket_info;
    struct table_s *fallback;
} Table_t;

typedef Table_t OptionalTable_t;

#define NONE_TABLE ((OptionalTable_t){.entries.data = NULL})
