// Boolean methods/type info
#include <gc.h>
#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/param.h>
#include <err.h>

#include "util.h"
#include "bool.h"
#include "halfsiphash.h"
#include "types.h"

public CORD Bool__as_text(const bool *b, bool colorize, const TypeInfo *type)
{
    (void)type;
    if (!b) return "Bool";
    if (colorize)
        return *b ? "\x1b[35myes\x1b[m" : "\x1b[35mno\x1b[m";
    else
        return *b ? "yes" : "no";
}

public const TypeInfo Bool = {
    .size=sizeof(bool),
    .align=__alignof__(bool),
    .tag=CustomInfo,
    .CustomInfo={.as_text=(void*)Bool__as_text},
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
