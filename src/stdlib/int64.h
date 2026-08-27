#define INTX_H__INT_BITS 64
#define I64(i) (int64_t)(i)
#include "intX.h" // IWYU pragma: export
#define NONE_INT64 ((OptionalInt64_t){.has_value = false})

int Int64$print(FILE *f, int64_t n);

// This header has no include guard of its own (it's an intX.h instantiation,
// meant to be included once from integers.h), so guard the shared helper:
#ifndef TOMO_INT64_DECIMAL
#define TOMO_INT64_DECIMAL

#define INT64_DECIMAL_MAX 20 // strlen("-9223372036854775808")

// Renders `n` in decimal into `buf`, filling from the right, and returns a
// pointer to the first character written, with its length in `*len`. The
// result is NOT NUL-terminated.
static inline char *int64_to_decimal(int64_t n, char buf[INT64_DECIMAL_MAX], size_t *len) {
    char *p = &buf[INT64_DECIMAL_MAX];
    // Take the magnitude in unsigned space: negating INT64_MIN overflows a
    // signed int64, and C's truncating % yields negative digits otherwise.
    bool negative = n < 0;
    uint64_t magnitude = negative ? -(uint64_t)n : (uint64_t)n;

    do {
        *(--p) = (char)('0' + (magnitude % 10));
        magnitude /= 10;
    } while (magnitude > 0);

    if (negative) *(--p) = '-';

    *len = (size_t)(&buf[INT64_DECIMAL_MAX] - p);
    return p;
}

#endif
