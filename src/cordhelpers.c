// Some helper functions for the GC Cord library

#include <gc/cord.h>
#include <stdint.h>

#include "cordhelpers.h"
#include "stdlib/print.h"
#include "stdlib/util.h"

public CORD CORD_quoted(CORD str)
{
    CORD quoted = "\"";
    CORD_pos i;
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif
    CORD_FOR(i, str) {
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
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
            quoted = CORD_all(quoted, "\\x", String(hex((uint64_t)c, .no_prefix=true, .uppercase=true, .digits=2)));
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
