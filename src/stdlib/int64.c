#define INTX_C_H__INT_BITS 64
#include "intX.c.h"
#undef INTX_C_H__INT_BITS

public
int Int64$print(FILE *f, int64_t n) {
    char buf[21] = {[20] = 0}; // Big enough for INT64_MIN + '\0'
    char *p = &buf[19];
    bool negative = n < 0;

    do {
        *(p--) = '0' + (n % 10);
        n /= 10;
    } while (n > 0);

    if (negative) *(p--) = '-';

    return fwrite(p + 1, sizeof(char), (size_t)(&buf[19] - p), f);
}
