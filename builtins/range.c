// Functions that operate on numeric ranges

#include <ctype.h>
#include <err.h>
#include <gc.h>
#include <gc/cord.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>

#include "types.h"
#include "util.h"


static int32_t Range$compare(const Range_t *x, const Range_t *y, const TypeInfo *type)
{
    (void)type;
    int32_t diff = (x->first > y->first) - (x->first < y->first);
    if (diff != 0) return diff;
    diff = (x->last > y->last) - (x->last < y->last);
    if (diff != 0) return diff;
    return (x->step > y->step) - (x->step < y->step);
}

static bool Range$equal(const Range_t *x, const Range_t *y, const TypeInfo *type)
{
    (void)type;
    return (x->first == y->first) && (x->last == y->last) && (x->step == y->step);
}

static CORD Range$as_text(const Range_t *r, bool use_color, const TypeInfo *type)
{
    (void)type;
    if (!r) return "Range";

    return CORD_asprintf(use_color ? "\x1b[0;1mRange\x1b[m(first=\x1b[35m%ld\x1b[m, last=\x1b[35m%ld\x1b[m, step=\x1b[35m%ld\x1b[m)"
                         : "Range(first=%ld, last=%ld, step=%ld)", r->first, r->last, r->step);
}

public Range_t Range$reversed(Range_t r)
{
    return (Range_t){r.last, r.first, -r.step};
}

public Range_t Range$by(Range_t r, int64_t step)
{
    return (Range_t){r.first, r.last, step*r.step};
}

public const TypeInfo Range = {sizeof(Range_t), __alignof(Range_t), {.tag=CustomInfo, .CustomInfo={
    .as_text=(void*)Range$as_text,
    .compare=(void*)Range$compare,
    .equal=(void*)Range$equal,
}}};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
