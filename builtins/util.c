// Built-in utility functions
#include <ctype.h>
#include <gc.h>
#include <gc/cord.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "text.h"
#include "util.h"

public bool USE_COLOR;

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

public CORD CORD_asprintf(CORD fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    CORD c = NULL;
    CORD_vsprintf(&c, fmt, args);
    va_end(args);
    return c;
}

public CORD CORD_quoted(CORD str)
{
    CORD quoted = "\"";
    CORD_pos i;
    CORD_FOR(i, str) {
        char c = CORD_pos_fetch(i);
        switch (c) {
        case '\a': quoted = CORD_cat(quoted, "\\a"); break;
        case '\b': quoted = CORD_cat(quoted, "\\b"); break;
        case '\x1b': quoted = CORD_cat(quoted, "\\e"); break;
        case '\f': quoted = CORD_cat(quoted, "\\f"); break;
        case '\n': quoted = CORD_cat(quoted, "\\n"); break;
        case '\r': quoted = CORD_cat(quoted, "\\r"); break;
        case '\t': quoted = CORD_cat(quoted, "\\t"); break;
        case '\v': quoted = CORD_cat(quoted, "\\v"); break;
        case '"': quoted = CORD_cat(quoted, "\\\""); break;
        case '\\': quoted = CORD_cat(quoted, "\\\\"); break;
        case '\x00' ... '\x06': case '\x0E' ... '\x1A':
        case '\x1C' ... '\x1F': case '\x7F' ... '\x7F':
            CORD_sprintf(&quoted, "%r\\x%02X", quoted, c);
            break;
        default: quoted = CORD_cat_char(quoted, c); break;
        }
    }
    quoted = CORD_cat_char(quoted, '"');
    return quoted;
}

public CORD CORD_replace(CORD c, CORD to_replace, CORD replacement)
{
    size_t len = CORD_len(c);
    size_t replaced_len = CORD_len(to_replace);
    size_t pos = 0;
    CORD ret = CORD_EMPTY;
    while (pos < len) {
        size_t found = CORD_str(c, pos, to_replace);
        if (found == CORD_NOT_FOUND) {
            if (pos < len)
                ret = CORD_cat(ret, CORD_substr(c, pos, len));
            return ret;
        }
        if (found > pos)
            ret = CORD_cat(ret, CORD_substr(c, pos, found-pos));
        ret = CORD_cat(ret, replacement);
        pos = found + replaced_len;
    }
    return ret;
}


// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
