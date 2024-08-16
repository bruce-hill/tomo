// Boolean methods/type info
#include <ctype.h>
#include <err.h>
#include <gc.h>
#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>

#include "bool.h"
#include "text.h"
#include "types.h"
#include "util.h"

public CORD Bool$as_text(const bool *b, bool colorize, const TypeInfo *type)
{
    (void)type;
    if (!b) return "Bool";
    if (colorize)
        return *b ? "\x1b[35myes\x1b[m" : "\x1b[35mno\x1b[m";
    else
        return *b ? "yes" : "no";
}

public Bool_t Bool$from_text(CORD text, bool *success)
{
    CORD lower = Text$lower(text);
    if (CORD_cmp(lower, "yes") == 0 || CORD_cmp(lower, "on") == 0
        || CORD_cmp(lower, "true") == 0 || CORD_cmp(lower, "1") == 0) {
        if (success) *success = yes;
        return yes;
    } else if (CORD_cmp(lower, "no") == 0 || CORD_cmp(lower, "off") == 0
               || CORD_cmp(lower, "false") == 0 || CORD_cmp(lower, "0") == 0) {
        if (success) *success = yes;
        return no;
    } else {
        if (success) *success = no;
        return no;
    }
}

public Bool_t Bool$random(double p)
{
    return (drand48() < p); 
}

public const TypeInfo $Bool = {
    .size=sizeof(bool),
    .align=__alignof__(bool),
    .tag=CustomInfo,
    .CustomInfo={.as_text=(void*)Bool$as_text},
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
