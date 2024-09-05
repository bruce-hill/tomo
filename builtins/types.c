// Type information and methods for TypeInfos (i.e. runtime representations of types)
#include <err.h>
#include <gc.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include "util.h"
#include "array.h"
#include "pointer.h"
#include "table.h"
#include "text.h"
#include "types.h"

public Text_t Type$as_text(const void *typeinfo, bool colorize, const TypeInfo *type)
{
    if (!typeinfo) return Text("TypeInfo");

    if (colorize)
        return Text$concat(
            Text("\x1b[36;1m"),
            Text$from_str(type->TypeInfoInfo.type_str),
            Text("\x1b[m"));
    else
        return Text$from_str(type->TypeInfoInfo.type_str);
}

public const TypeInfo TypeInfo$info = {
    .size=sizeof(TypeInfo),
    .align=__alignof__(TypeInfo),
    .tag=CustomInfo,
    .TypeInfoInfo.type_str="TypeInfo",
};

public const TypeInfo Void$info = {.size=0, .align=0, .tag=EmptyStruct};
public const TypeInfo Abort$info = {.size=0, .align=0, .tag=EmptyStruct};

public Text_t Func$as_text(const void *fn, bool colorize, const TypeInfo *type)
{
    (void)fn;
    Text_t text = Text$from_str(type->FunctionInfo.type_str);
    if (fn && colorize)
        text = Text$concat(Text("\x1b[32;1m"), text, Text("\x1b[m"));
    return text;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
