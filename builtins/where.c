// A type called "Where" that is an enum for "Anywhere", "Start", or "End"
// Mainly used for text methods

#include <stdbool.h>
#include <stdint.h>

#include "text.h"
#include "types.h"
#include "util.h"
#include "where.h"

static Text_t Where$as_text(Where_t *obj, bool use_color)
{
    if (!obj)
        return Text$from_str("Where");
    switch (obj->tag) {
    case $tag$Where$Anywhere:
        return Text$from_str(use_color ? "\x1b[36;1mWhere.Anywhere\x1b[m" : "Where.Anywhere");
    case $tag$Where$Start:
        return Text$from_str(use_color ? "\x1b[36;1mWhere.Start\x1b[m" : "Where.Start");
    case $tag$Where$End:
        return Text$from_str(use_color ? "\x1b[36;1mWhere.End\x1b[m" : "Where.End");
    default:
        return (Text_t){.length=0};
    }
}

public const Where_t Where$tagged$Anywhere = {$tag$Where$Anywhere};
public const Where_t Where$tagged$Start = {$tag$Where$Start};
public const Where_t Where$tagged$End = {$tag$Where$End};
public const TypeInfo Where$Anywhere = {0, 0, {.tag=EmptyStruct, .EmptyStruct.name="Anywhere"}};
public const TypeInfo Where$Start = {0, 0, {.tag=EmptyStruct, .EmptyStruct.name="Start"}};
public const TypeInfo Where$End = {0, 0, {.tag=EmptyStruct, .EmptyStruct.name="End"}};
public const TypeInfo Where = {sizeof(Where_t), __alignof__(Where_t),
    {.tag=CustomInfo, .CustomInfo={.as_text=(void*)Where$as_text}}};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
