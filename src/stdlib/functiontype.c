// Logic for handling function type values

#include <stdbool.h>

#include "datatypes.h"
#include "functiontype.h"
#include "structs.h"
#include "text.h"
#include "types.h"
#include "util.h"

public
Text_t Func$as_text(const void *fn, bool colorize, const TypeInfo_t *type) {
    Text_t text = Text$from_str(type->FunctionInfo.type_str);
    if (fn && colorize) text = Text$concat(Text("\x1b[32;1m"), text, Text("\x1b[m"));
    return text;
}

public
PUREFUNC bool Func$is_none(const void *obj, const TypeInfo_t *info) {
    (void)info;
    return ((Closure_t *)obj)->fn == NULL;
}
