#pragma once

#include <stdbool.h>

#include "types.h"
#include "util.h"

// Logic for handling function type values

void register_function(void *fn, Text_t name);
Text_t *get_function_name(void *fn);
Text_t Func$as_text(const void *fn, bool colorize, const TypeInfo_t *type);
PUREFUNC bool Func$is_none(const void *obj, const TypeInfo_t*);

#define Func$metamethods ((metamethods_t){ \
    .as_text=Func$as_text, \
    .is_none=Func$is_none, \
})

#define Function$info(typestr) &((TypeInfo_t){.size=sizeof(void*), .align=__alignof__(void*), \
                                 .tag=FunctionInfo, .FunctionInfo.type_str=typestr, \
                                 .metamethods=Func$metamethods})
#define Closure$info(typestr) &((TypeInfo_t){.size=sizeof(void*[2]), .align=__alignof__(void*), \
                                 .tag=FunctionInfo, .FunctionInfo.type_str=typestr, \
                                 .metamethods=Func$metamethods})

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
