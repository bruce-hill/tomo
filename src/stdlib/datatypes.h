#pragma once

// Common datastructures (lists, tables, closures)

#include <gmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define LIST_LENGTH_BITS 42
#define LIST_FREE_BITS 6
#define LIST_REFCOUNT_BITS 3
#define LIST_STRIDE_BITS 12

#define MAX_FOR_N_BITS(N) ((1<<(N))-1)
#define LIST_MAX_STRIDE MAX_FOR_N_BITS(LIST_STRIDE_BITS-1)
#define LIST_MIN_STRIDE (~MAX_FOR_N_BITS(LIST_STRIDE_BITS-1))
#define LIST_MAX_DATA_REFCOUNT MAX_FOR_N_BITS(LIST_REFCOUNT_BITS)
#define LIST_MAX_FREE_ENTRIES MAX_FOR_N_BITS(LIST_FREE_BITS)

#define Num_t double
#define Num32_t float

#define Int64_t int64_t
#define Int32_t int32_t
#define Int16_t int16_t
#define Int8_t int8_t
#define Byte_t uint8_t
#define Bool_t bool

typedef union {
    int64_t small;
    mpz_t *big;
} Int_t;

#define OptionalInt_t Int_t

typedef struct {
    void *data;
    // All of the following fields add up to 64 bits, which means that list
    // structs can be passed in two 64-bit registers. C will handle doing the
    // bit arithmetic to extract the necessary values, which is cheaper than
    // spilling onto the stack and needing to retrieve data from the stack.
    int64_t length:LIST_LENGTH_BITS;
    uint8_t free:LIST_FREE_BITS;
    bool atomic:1;
    uint8_t data_refcount:LIST_REFCOUNT_BITS;
    int16_t stride:LIST_STRIDE_BITS;
} List_t;

typedef struct {
    uint32_t occupied:1, index:31;
    uint32_t next_bucket;
} bucket_t;

#define TABLE_MAX_BUCKETS 0x7fffffff
#define TABLE_MAX_DATA_REFCOUNT 3

typedef struct {
    uint32_t count:31, last_free:31;
    uint8_t data_refcount:2;
    bucket_t buckets[];
} bucket_info_t;

typedef struct table_s {
    List_t entries;
    uint64_t hash;
    bucket_info_t *bucket_info;
    struct table_s *fallback;
} Table_t;

typedef struct {
    void *fn, *userdata;
} Closure_t;

enum text_type { TEXT_ASCII, TEXT_GRAPHEMES, TEXT_CONCAT };

typedef struct Text_s {
    int64_t length:54; // Number of grapheme clusters
    uint8_t tag:2;
    uint8_t depth:8;
    union {
        struct {
            const char *ascii;
            // char ascii_buf[8];
        };
        struct {
            const int32_t *graphemes;
            // int32_t grapheme_buf[2];
        };
        struct {
            const struct Text_s *left, *right;
        };
    };
} Text_t;

typedef struct {
    enum { PATH_NONE, PATH_RELATIVE, PATH_ABSOLUTE, PATH_HOME } $tag;
} PathType_t;
#define OptionalPathType_t PathType_t

typedef struct {
    PathType_t type;
    List_t components;
} Path_t;
#define OptionalPath_t Path_t

#define OptionalBool_t uint8_t
#define OptionalList_t List_t
#define OptionalTable_t Table_t
#define OptionalText_t Text_t
#define OptionalClosure_t Closure_t

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
