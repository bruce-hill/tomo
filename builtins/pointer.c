// Type infos and methods for Pointer types
#include <ctype.h>
#include <err.h>
#include <gc.h>
#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>

#include "util.h"
#include "functions.h"
#include "halfsiphash.h"
#include "types.h"

typedef struct recursion_s {
    const void *ptr;
    struct recursion_s *next;
} recursion_t;

public CORD Pointer$as_text(const void *x, bool colorize, const TypeInfo *type) {
    auto ptr_info = type->PointerInfo;
    if (!x) {
        CORD typename = generic_as_text(NULL, false, ptr_info.pointed);
        CORD c = colorize ? CORD_asprintf("\x1b[34;1m%s%s\x1b[m", ptr_info.sigil, typename) : CORD_cat(ptr_info.sigil, typename);
        return ptr_info.is_optional ? CORD_cat(c, "?") : c;
    }
    const void *ptr = *(const void**)x;
    if (!ptr) {
        CORD typename = generic_as_text(NULL, false, ptr_info.pointed);
        return colorize ? CORD_asprintf("\x1b[34;1m!%s\x1b[m", typename) : CORD_cat("!", typename);
    }

    // Check for recursive references, so if `x.foo = x`, then it prints as
    // `@Foo{foo=@..1}` instead of overflowing the stack:
    static recursion_t *recursion = NULL;
    int32_t depth = 0;
    for (recursion_t *r = recursion; r; r = r->next) {
        ++depth;
        if (r->ptr == ptr) {
            CORD c = CORD_asprintf(colorize ? "\x1b[34;1m%s..%d\x1b[m" : "%s..%d", ptr_info.sigil, depth);
            if (ptr_info.is_optional) c = CORD_cat(c, colorize ? "\x1b[34;1m?\x1b[m" : "?");
            return c;
        }
    }

    CORD pointed;
    { // Stringify with this pointer flagged as a recursive one:
        recursion_t my_recursion = {.ptr=ptr, .next=recursion};
        recursion = &my_recursion;
        pointed = generic_as_text(ptr, colorize, ptr_info.pointed);
        recursion = recursion->next;
    }
    CORD c = colorize ? CORD_asprintf("\x1b[34;1m%s\x1b[m%r", ptr_info.sigil, pointed) : CORD_cat(ptr_info.sigil, pointed);
    if (ptr_info.is_optional) c = CORD_cat(c, colorize ? "\x1b[34;1m?\x1b[m" : "?");
    return c;
}

public int32_t Pointer$compare(const void *x, const void *y, const TypeInfo *type) {
    (void)type;
    const void *xp = *(const void**)x, *yp = *(const void**)y;
    return (xp > yp) - (xp < yp);
}

public bool Pointer$equal(const void *x, const void *y, const TypeInfo *type) {
    (void)type;
    const void *xp = *(const void**)x, *yp = *(const void**)y;
    return xp == yp;
}

public uint32_t Pointer$hash(const void *x, const TypeInfo *type) {
    (void)type;
    uint32_t hash;
    halfsiphash(x, sizeof(void*), TOMO_HASH_KEY, (uint8_t*)&hash, sizeof(hash));
    return hash;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
