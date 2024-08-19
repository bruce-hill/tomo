// A type called "Where" that is an enum for "Anywhere", "Start", or "End"
// Mainly used for text methods

#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>

#include "types.h"
#include "where.h"
#include "util.h"

static CORD Where$Anywhere$as_text(Where$Anywhere_t *obj, bool use_color)
{
    if (!obj) return "Anywhere";
    return CORD_all(use_color ? "\x1b[0;1mAnywhere\x1b[m(" : "Anywhere(", ")");
}

static CORD Where$Start$as_text(Where$Start_t *obj, bool use_color)
{
    if (!obj) return "Start";
    return CORD_all(use_color ? "\x1b[0;1mStart\x1b[m(" : "Start(", ")");
}

static CORD Where$End$as_text(Where$End_t *obj, bool use_color)
{
    if (!obj) return "End";
    return CORD_all(use_color ? "\x1b[0;1mEnd\x1b[m(" : "End(", ")");
}

static CORD Where$as_text(Where_t *obj, bool use_color)
{
    if (!obj)
        return "Where";
    switch (obj->tag) {
    case $tag$Where$Anywhere:
        return use_color ? "\x1b[36;1mWhere.Anywhere\x1b[m" : "Where.Anywhere";
    case $tag$Where$Start:
        return use_color ? "\x1b[36;1mWhere.Start\x1b[m" : "Where.Start";
    case $tag$Where$End:
        return use_color ? "\x1b[36;1mWhere.End\x1b[m" : "Where.End";
    default:
        return CORD_EMPTY;
    }
}

public const Where_t Where$tagged$Anywhere = {$tag$Where$Anywhere};
public const Where_t Where$tagged$Start = {$tag$Where$Start};
public const Where_t Where$tagged$End = {$tag$Where$End};
public const TypeInfo Where$Anywhere = {0, 0, {.tag=CustomInfo, .CustomInfo={.as_text=(void*)Where$Anywhere$as_text}}};
public const TypeInfo Where$Start = {0, 0, {.tag=CustomInfo, .CustomInfo={.as_text=(void*)Where$Start$as_text}}};
public const TypeInfo Where$End = {0, 0, {.tag=CustomInfo, .CustomInfo={.as_text=(void*)Where$End$as_text}}};
public const TypeInfo Where = {4, 4, {.tag=CustomInfo, .CustomInfo={.as_text=(void*)Where$as_text}}};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
