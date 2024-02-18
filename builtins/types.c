// Generic type constructor
#include <err.h>
#include <gc.h>
#include <string.h>
#include <stdalign.h>
#include <stdlib.h>
#include <sys/param.h>

#include "array.h"
#include "table.h"
#include "pointer.h"
#include "types.h"
#include "../util.h"
#include "../SipHash/halfsiphash.h"

public CORD Type__as_str(const void *typeinfo, bool colorize, const TypeInfo *type)
{
    if (!typeinfo) return "TypeInfo";

    if (!colorize)
        return type->TypeInfoInfo.type_str;
    CORD c;
    CORD_sprintf(&c, "\x1b[36;1m%s\x1b[m", type->TypeInfoInfo.type_str);
    return c;
}

public struct {
    TypeInfo type;
} TypeInfo_type = {
    .type={
        .size=sizeof(TypeInfo),
        .align=alignof(TypeInfo),
        .tag=CustomInfo,
        .TypeInfoInfo.type_str="TypeInfo",
    },
};

public struct {
    TypeInfo type;
} Void_type = {.type={.size=0, .align=0}};
public struct {
    TypeInfo type;
} Abort_type = {.type={.size=0, .align=0}};

public CORD Func__as_str(const void *fn, bool colorize, const TypeInfo *type)
{
    (void)fn;
    CORD c = type->FunctionInfo.type_str;
    if (fn && colorize)
        CORD_sprintf(&c, "\x1b[32;1m%r\x1b[m", c);
    return c;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
