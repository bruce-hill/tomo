// Built-in utility functions
#include <ctype.h>
#include <gc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "text.h"
#include "util.h"

__attribute__((format(printf, 1, 2)))
public char *heap_strf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char *tmp = NULL;
    int len = vasprintf(&tmp, fmt, args);
    if (len < 0) return NULL;
    va_end(args);
    char *ret = GC_strndup(tmp, (size_t)len);
    free(tmp);
    return ret;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
