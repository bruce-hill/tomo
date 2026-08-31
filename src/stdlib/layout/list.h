// Representation of the List type

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LIST_LENGTH_BITS 64
#define LIST_FREE_BITS 48
#define LIST_ATOMIC_BITS 1
#define LIST_REFCOUNT_BITS 3
#define LIST_STRIDE_BITS 12

#define MAX_FOR_N_BITS(N) ((1L << (N)) - 1L)
#define LIST_MAX_STRIDE MAX_FOR_N_BITS(LIST_STRIDE_BITS - 1)
#define LIST_MIN_STRIDE (~MAX_FOR_N_BITS(LIST_STRIDE_BITS - 1))
#define LIST_MAX_DATA_REFCOUNT MAX_FOR_N_BITS(LIST_REFCOUNT_BITS)
#define LIST_MAX_FREE_ENTRIES MAX_FOR_N_BITS(LIST_FREE_BITS)

typedef struct {
    void *data;
    // All of the following fields add up to 64 bits, which means that list
    // structs can be passed in two 64-bit registers. C will handle doing the
    // bit arithmetic to extract the necessary values, which is cheaper than
    // spilling onto the stack and needing to retrieve data from the stack.
    uint64_t length : LIST_LENGTH_BITS;
    uint64_t free : LIST_FREE_BITS;
    bool atomic : LIST_ATOMIC_BITS;
    uint8_t data_refcount : LIST_REFCOUNT_BITS;
    int16_t stride : LIST_STRIDE_BITS;
} List_t;

typedef List_t OptionalList_t;

// A list always has a data pointer (the empty list points at a shared
// sentinel byte), so a NULL one is free to mean `none`.
#define NONE_LIST ((List_t){.data = NULL})
