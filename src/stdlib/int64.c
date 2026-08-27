#define INTX_C_H__INT_BITS 64
#include "intX.c.h"
#undef INTX_C_H__INT_BITS

public
int Int64$print(FILE *f, int64_t n) {
    char buf[INT64_DECIMAL_MAX];
    size_t len;
    char *digits = int64_to_decimal(n, buf, &len);
    return (int)fwrite(digits, sizeof(char), len, f);
}
