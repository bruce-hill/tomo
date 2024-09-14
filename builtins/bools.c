// Boolean methods/type info
#include <ctype.h>
#include <err.h>
#include <gc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/param.h>

#include "bools.h"
#include "optionals.h"
#include "text.h"
#include "util.h"

PUREFUNC public Text_t Bool$as_text(const bool *b, bool colorize, const TypeInfo *type)
{
    (void)type;
    if (!b) return Text("Bool");
    if (colorize)
        return *b ? Text("\x1b[35myes\x1b[m") : Text("\x1b[35mno\x1b[m");
    else
        return *b ? Text("yes") : Text("no");
}

PUREFUNC public OptionalBool_t Bool$from_text(Text_t text)
{
    if (Text$equal_ignoring_case(text, Text("yes"))
        || Text$equal_ignoring_case(text, Text("on"))
        || Text$equal_ignoring_case(text, Text("true"))
        || Text$equal_ignoring_case(text, Text("1"))) {
        return yes;
    } else if (Text$equal_ignoring_case(text, Text("no"))
        || Text$equal_ignoring_case(text, Text("off"))
        || Text$equal_ignoring_case(text, Text("false"))
        || Text$equal_ignoring_case(text, Text("0"))) {
        return no;
    } else {
        return NULL_BOOL;
    }
}

public Bool_t Bool$random(double p)
{
    return (drand48() < p); 
}

public const TypeInfo Bool$info = {
    .size=sizeof(bool),
    .align=__alignof__(bool),
    .tag=CustomInfo,
    .CustomInfo={.as_text=(void*)Bool$as_text},
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
