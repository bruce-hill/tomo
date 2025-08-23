// Type information and methods for TypeInfos (i.e. runtime representations of types)
#include <err.h>
#include <gc.h>
#include <sys/param.h>

#include "text.h"
#include "types.h"
#include "util.h"

public
Text_t Type$as_text(const void *typeinfo, bool colorize, const TypeInfo_t *type) {
    if (!typeinfo) return Text("Type");

    if (colorize) return Text$concat(Text("\x1b[36;1m"), Text$from_str(type->TypeInfoInfo.type_str), Text("\x1b[m"));
    else return Text$from_str(type->TypeInfoInfo.type_str);
}

public
const TypeInfo_t Void$info = {.size = 0, .align = 0, .tag = StructInfo};
public
const TypeInfo_t Abort$info = {.size = 0, .align = 0, .tag = StructInfo};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
