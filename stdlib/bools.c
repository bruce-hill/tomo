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

PUREFUNC public Text_t Bool$as_text(const void *b, bool colorize, const TypeInfo_t*)
{
    if (!b) return Text("Bool");
    if (colorize)
        return *(Bool_t*)b ? Text("\x1b[35myes\x1b[m") : Text("\x1b[35mno\x1b[m");
    else
        return *(Bool_t*)b ? Text("yes") : Text("no");
}

PUREFUNC public OptionalBool_t Bool$parse(Text_t text)
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
        return NONE_BOOL;
    }
}

static bool Bool$is_none(const void *b, const TypeInfo_t*)
{
    return *(OptionalBool_t*)b == NONE_BOOL;
}

static const metamethods_t Bool$metamethods = {
    .as_text=Bool$as_text,
    .is_none=Bool$is_none,
};

public const TypeInfo_t Bool$info = {
    .size=sizeof(bool),
    .align=__alignof__(bool),
    .metamethods=Bool$metamethods,
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
