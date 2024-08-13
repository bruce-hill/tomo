// Functions that operate on numeric ranges

#include <ctype.h>
#include <err.h>
#include <gmp.h>
#include <gc.h>
#include <gc/cord.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>

#include "types.h"
#include "integers.h"
#include "util.h"


static int32_t Range$compare(const Range_t *x, const Range_t *y, const TypeInfo *type)
{
    (void)type;
    int32_t diff = Int$compare(&x->first, &y->first, &$Int);
    if (diff != 0) return diff;
    diff = Int$compare(&x->last, &y->last, &$Int);
    if (diff != 0) return diff;
    return Int$compare(&x->step, &y->step, &$Int);
}

static bool Range$equal(const Range_t *x, const Range_t *y, const TypeInfo *type)
{
    (void)type;
    return Int$equal(&x->first, &y->first, &$Int) && Int$equal(&x->last, &y->last, &$Int) && Int$equal(&x->step, &y->step, &$Int);
}

static CORD Range$as_text(const Range_t *r, bool use_color, const TypeInfo *type)
{
    (void)type;
    if (!r) return "Range";

    return CORD_asprintf(use_color ? "\x1b[0;1mRange\x1b[m(first=%r, last=%r, step=%r)"
                         : "Range(first=%r, last=%r, step=%r)",
                         Int$as_text(&r->first, use_color, &$Int), Int$as_text(&r->last, use_color, &$Int),
                         Int$as_text(&r->step, use_color, &$Int));
}

public Range_t Range$reversed(Range_t r)
{
    return (Range_t){r.last, r.first, Int$negative(r.step)};
}

public Range_t Range$by(Range_t r, Int_t step)
{
    return (Range_t){r.first, r.last, Int$times(step, r.step)};
}

public const TypeInfo Range = {sizeof(Range_t), __alignof(Range_t), {.tag=CustomInfo, .CustomInfo={
    .as_text=(void*)Range$as_text,
    .compare=(void*)Range$compare,
    .equal=(void*)Range$equal,
}}};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
