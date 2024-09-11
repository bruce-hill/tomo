// Type infos and methods for Pointer types
#include <ctype.h>
#include <err.h>
#include <gc.h>
#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>

#include "functions.h"
#include "text.h"
#include "types.h"
#include "util.h"

typedef struct recursion_s {
    const void *ptr;
    struct recursion_s *next;
} recursion_t;

public Text_t Pointer$as_text(const void *x, bool colorize, const TypeInfo *type) {
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

    // Check for recursive references, so if `x.foo = x`, then it prints as
    // `@Foo{foo=@..1}` instead of overflowing the stack:
    static recursion_t *recursion = NULL;
    int32_t depth = 0;
    for (recursion_t *r = recursion; r; r = r->next) {
        ++depth;
        if (r->ptr == ptr) {
            Text_t text = Text$concat(
                colorize ? Text("\x1b[34;1m") : Text(""),
                Text$from_str(ptr_info.sigil),
                Text(".."),
                Int32$as_text(&depth, false, &Int32$info),
                colorize ? Text("\x1b[m") : Text(""));
            return text;
        }
    }

    Text_t pointed;
    { // Stringify with this pointer flagged as a recursive one:
        recursion_t my_recursion = {.ptr=ptr, .next=recursion};
        recursion = &my_recursion;
        pointed = generic_as_text(ptr, colorize, ptr_info.pointed);
        recursion = recursion->next;
    }
    Text_t text;
    if (colorize)
        text = Text$concat(Text("\x1b[34;1m"), Text$from_str(ptr_info.sigil), Text("\x1b[m"), pointed);
    else
        text = Text$concat(Text$from_str(ptr_info.sigil), pointed);
    return text;
}

PUREFUNC public int32_t Pointer$compare(const void *x, const void *y, const TypeInfo *type) {
    (void)type;
    const void *xp = *(const void**)x, *yp = *(const void**)y;
    return (xp > yp) - (xp < yp);
}

PUREFUNC public bool Pointer$equal(const void *x, const void *y, const TypeInfo *type) {
    (void)type;
    const void *xp = *(const void**)x, *yp = *(const void**)y;
    return xp == yp;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
