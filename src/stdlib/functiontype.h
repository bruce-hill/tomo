#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "types.h"
#include "util.h"

// Logic for handling function type values

void register_function(void *fn, Text_t filename, int64_t line_num, Text_t name);
OptionalText_t get_function_name(void *fn);
OptionalText_t get_function_filename(void *fn);
int64_t get_function_line_num(void *fn);
Text_t Func$as_text(const void *fn, bool colorize, const TypeInfo_t *type);
PUREFUNC bool Func$is_none(const void *obj, const TypeInfo_t*);

#define Func$metamethods { \
    .as_text=Func$as_text, \
    .is_none=Func$is_none, \
    .serialize=cannot_serialize, \
    .deserialize=cannot_deserialize, \
}

#define Function$info(typestr) &((TypeInfo_t){.size=sizeof(void*), .align=__alignof__(void*), \
                                 .tag=FunctionInfo, .FunctionInfo.type_str=typestr, \
                                 .metamethods=Func$metamethods})
#define Closure$info(typestr) &((TypeInfo_t){.size=sizeof(void*[2]), .align=__alignof__(void*), \
                                 .tag=FunctionInfo, .FunctionInfo.type_str=typestr, \
                                 .metamethods=Func$metamethods})

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
