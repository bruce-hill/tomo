// Logic for handling function type values

#include <stdbool.h>

#include "datatypes.h"
#include "functiontype.h"
#include "optionals.h"
#include "structs.h"
#include "tables.h"
#include "text.h"
#include "types.h"
#include "util.h"

public Text_t Funcヽas_text(const void *fn, bool colorize, const TypeInfo_t *type)
{
    Text_t text = Textヽfrom_str(type->FunctionInfo.type_str);
    if (fn && colorize)
        text = Textヽconcat(Text("\x1b[32;1m"), text, Text("\x1b[m"));
    return text;
}

public PUREFUNC bool Funcヽis_none(const void *obj, const TypeInfo_t *info)
{
    (void)info;
    return *(void**)obj == NULL;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
