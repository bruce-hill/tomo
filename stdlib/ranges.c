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


PUREFUNC static int32_t Range$compare(const void *vx, const void *vy, const TypeInfo_t *)
{
    if (vx == vy) return 0;
    Range_t *x = (Range_t*)vx, *y = (Range_t*)vy;
    int32_t diff = Int$compare(&x->first, &y->first, &Int$info);
    if (diff != 0) return diff;
    diff = Int$compare(&x->last, &y->last, &Int$info);
    if (diff != 0) return diff;
    return Int$compare(&x->step, &y->step, &Int$info);
}

PUREFUNC static bool Range$equal(const void *vx, const void *vy, const TypeInfo_t*)
{
    if (vx == vy) return true;
    Range_t *x = (Range_t*)vx, *y = (Range_t*)vy;
    return Int$equal(&x->first, &y->first, &Int$info) && Int$equal(&x->last, &y->last, &Int$info) && Int$equal(&x->step, &y->step, &Int$info);
}

static Text_t Range$as_text(const void *obj, bool use_color, const TypeInfo_t *)
{
    if (!obj) return Text("Range");

    Range_t *r = (Range_t*)obj;
    Text_t first = Int$as_text(&r->first, use_color, &Int$info);
    Text_t last = Int$as_text(&r->last, use_color, &Int$info);
    Text_t step = Int$as_text(&r->step, use_color, &Int$info);
    return Text$format(use_color ? "\x1b[0;1mRange\x1b[m(first=%k, last=%k, step=%k)"
                       : "Range(first=%k, last=%k, step=%k)", &first, &last, &step);
}

PUREFUNC public Range_t Range$reversed(Range_t r)
{
    return (Range_t){r.last, r.first, Int$negative(r.step)};
}

PUREFUNC public Range_t Range$by(Range_t r, Int_t step)
{
    return (Range_t){r.first, r.last, Int$times(step, r.step)};
}

static bool Range$is_none(const void *obj, const TypeInfo_t*)
{
    return ((Range_t*)obj)->step.small == 0x1;
}

public const TypeInfo_t Range$info = {
    .size=sizeof(Range_t),
    .align=__alignof(Range_t),
    .metamethods={
        .as_text=Range$as_text,
        .compare=Range$compare,
        .equal=Range$equal,
        .is_none=Range$is_none,
    },
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
