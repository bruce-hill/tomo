// Built-in utility functions
#include <ctype.h>
#include <gc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "text.h"
#include "util.h"

public bool USE_COLOR;

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

// Name mangling algorithm to produce valid identifiers:
// Escape individual chars as "_x%02X" 
// Things being escaped:
// - Leading digit
// - Non alphanumeric/non-underscore characters
// - "_" when followed by "x" and two uppercase hex digits
public char *mangle(const char *name)
{
    size_t len = 0;
    for (const char *p = name; *p; p++) {
        if ((!isalnum(*p) && *p != '_') // Non-identifier character
            || (p == name && isdigit(*p)) // Leading digit
            || (p[0] == '_' && p[1] == 'x' && strspn(p+2, "ABCDEF0123456789") >= 2)) { // Looks like hex escape
            len += strlen("_x00"); // Hex escape
        } else {
            len += 1;
        }
    }
    char *mangled = GC_MALLOC_ATOMIC(len + 1);
    char *dest = mangled;
    for (const char *src = name; *src; src++) {
        if ((!isalnum(*src) && *src != '_') // Non-identifier character
            || (src == name && isdigit(*src)) // Leading digit
            || (src[0] == '_' && src[1] == 'x' && strspn(src+2, "ABCDEF0123456789") >= 2)) { // Looks like hex escape
            dest += sprintf(dest, "_x%02X", *src); // Hex escape
        } else {
            *(dest++) = *src;
        }
    }
    mangled[len] = '\0';
    return mangled;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
