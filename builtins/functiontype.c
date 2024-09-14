// Logic for handling function type values

#include "datatypes.h"
#include "tables.h"
#include "text.h"
#include "types.h"
#include "util.h"

static Table_t function_names = {};

public void register_function(void *fn, Text_t name)
{
    Table$set(&function_names, &fn, &name, Table$info(Function$info("???"), &Text$info));
}

public Text_t *get_function_name(void *fn)
{
    return Table$get(function_names, &fn, Table$info(Function$info("???"), &Text$info));
}

public Text_t Func$as_text(const void *fn, bool colorize, const TypeInfo *type)
{
    (void)fn;
    Text_t text = Text$from_str(type->FunctionInfo.type_str);
    if (fn) {
        Text_t *name = get_function_name(*(void**)fn);
        if (name)
            text = *name;
    }
    if (fn && colorize)
        text = Text$concat(Text("\x1b[32;1m"), text, Text("\x1b[m"));
    return text;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
