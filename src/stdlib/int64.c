#define INTX_C_H__INT_BITS 64
#include "intX.c.h"
#undef INTX_C_H__INT_BITS

public
int Int64$print(FILE *f, int64_t n) {
    char buf[21] = {[20] = 0}; // Big enough for INT64_MIN + '\0'
    char *p = &buf[19];
    bool negative = n < 0;
    // Take the magnitude in unsigned space: negating INT64_MIN overflows a
    // signed int64, and C's truncating % yields negative digits otherwise.
    uint64_t magnitude = negative ? -(uint64_t)n : (uint64_t)n;

    do {
        *(p--) = (char)('0' + (magnitude % 10));
        magnitude /= 10;
    } while (magnitude > 0);

    if (negative) *(p--) = '-';

    return fwrite(p + 1, sizeof(char), (size_t)(&buf[19] - p), f);
}
