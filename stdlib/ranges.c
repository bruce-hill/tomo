// Functions that operate on numeric ranges

#include <ctype.h>
#include <err.h>
#include <gc.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>

#include "integers.h"
#include "text.h"
#include "types.h"
#include "util.h"


PUREFUNC static int32_t Range$compare(const Range_t *x, const Range_t *y, const TypeInfo_t *type)
{
    (void)type;
    if (x == y) return 0;
    int32_t diff = Int$compare(&x->first, &y->first, &Int$info);
    if (diff != 0) return diff;
    diff = Int$compare(&x->last, &y->last, &Int$info);
    if (diff != 0) return diff;
    return Int$compare(&x->step, &y->step, &Int$info);
}

PUREFUNC static bool Range$equal(const Range_t *x, const Range_t *y, const TypeInfo_t *type)
{
    (void)type;
    if (x == y) return true;
    return Int$equal(&x->first, &y->first, &Int$info) && Int$equal(&x->last, &y->last, &Int$info) && Int$equal(&x->step, &y->step, &Int$info);
}

static Text_t Range$as_text(const Range_t *r, bool use_color, const TypeInfo_t *type)
{
    (void)type;
    if (!r) return Text("Range");

    return Text$format(use_color ? "\x1b[0;1mRange\x1b[m(first=%r, last=%r, step=%r)"
                       : "Range(first=%r, last=%r, step=%r)",
                       Int$as_text(&r->first, use_color, &Int$info), Int$as_text(&r->last, use_color, &Int$info),
                       Int$as_text(&r->step, use_color, &Int$info));
}

PUREFUNC public Range_t Range$reversed(Range_t r)
{
    return (Range_t){r.last, r.first, Int$negative(r.step)};
}

PUREFUNC public Range_t Range$by(Range_t r, Int_t step)
{
    return (Range_t){r.first, r.last, Int$times(step, r.step)};
}

public const TypeInfo_t Range$info = {sizeof(Range_t), __alignof(Range_t), {.tag=CustomInfo, .CustomInfo={
    .as_text=(void*)Range$as_text,
    .compare=(void*)Range$compare,
    .equal=(void*)Range$equal,
}}};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
