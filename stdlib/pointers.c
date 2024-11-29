// Type infos and methods for Pointer types
#include <ctype.h>
#include <err.h>
#include <gc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>

#include "integers.h"
#include "metamethods.h"
#include "tables.h"
#include "text.h"
#include "types.h"
#include "util.h"

public Text_t Pointer$as_text(const void *x, bool colorize, const TypeInfo_t *type) {
    auto ptr_info = type->PointerInfo;
    if (!x) {
        Text_t typename = generic_as_text(NULL, false, ptr_info.pointed);
        Text_t text;
        if (colorize)
            text = Text$concat(Text("\x1b[34;1m"), Text$from_str(ptr_info.sigil), typename, Text("\x1b[m"));
        else
            text = Text$concat(Text$from_str(ptr_info.sigil), typename);
        return text;
    }
    const void *ptr = *(const void**)x;
    if (!ptr) {
        Text_t typename = generic_as_text(NULL, false, ptr_info.pointed);
        if (colorize)
            return Text$concat(Text("\x1b[34;1m!"), typename, Text("\x1b[m"));
        else
            return Text$concat(Text("!"), typename);
    }

    static const void *root = NULL;
    static Table_t pending = {};
    bool top_level = (root == NULL);

    // Check for recursive references, so if `x.foo = x`, then it prints as
    // `@Foo{foo=@~1}` instead of overflowing the stack:
    if (top_level) {
        root = ptr;
    } else if (ptr == root) {
        return Text$format(colorize ? "\x1b[34;1m%s~1\x1b[m" : "%s~1", ptr_info.sigil);
    } else {
        TypeInfo_t rec_table = *Table$info(type, &Int64$info);
        int64_t *id = Table$get(pending, x, &rec_table);
        if (id)
            return Text$format(colorize ? "\x1b[34;1m%s~%ld\x1b[m" : "%s~%ld", ptr_info.sigil, *id);
        int64_t next_id = pending.entries.length + 2;
        Table$set(&pending, x, &next_id, &rec_table);
    }

    Text_t pointed = generic_as_text(ptr, colorize, ptr_info.pointed);

    if (top_level) {
        pending = (Table_t){}; // Restore
        root = NULL;
    }

    Text_t text;
    if (colorize)
        text = Text$concat(Text("\x1b[34;1m"), Text$from_str(ptr_info.sigil), Text("\x1b[m"), pointed);
    else
        text = Text$concat(Text$from_str(ptr_info.sigil), pointed);
    return text;
}

PUREFUNC public int32_t Pointer$compare(const void *x, const void *y, const TypeInfo_t*) {
    const void *xp = *(const void**)x, *yp = *(const void**)y;
    return (xp > yp) - (xp < yp);
}

PUREFUNC public bool Pointer$equal(const void *x, const void *y, const TypeInfo_t*) {
    const void *xp = *(const void**)x, *yp = *(const void**)y;
    return xp == yp;
}

PUREFUNC public bool Pointer$is_none(const void *x, const TypeInfo_t*)
{
    return *(void**)x == NULL;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
