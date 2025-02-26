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

typedef struct {
    Text_t filename, name;
    int64_t line_num;
} func_info_t;

static NamedType_t fields[] = {
    {.name="filename", .type=&Text$info},
    {.name="name", .type=&Text$info},
    {.name="line_num", .type=&Int64$info},
};

static const TypeInfo_t func_info_type = {.size=sizeof(func_info_t), .align=__alignof__(func_info_t), .metamethods=Struct$metamethods,
                                  .tag=StructInfo, .StructInfo.name="FuncInfo",
                                  .StructInfo.num_fields=3, .StructInfo.fields=fields};
static Table_t function_info = {};

public void register_function(void *fn, Text_t filename, int64_t line_num, Text_t name)
{
    func_info_t info = {
        .filename=filename,
        .line_num=line_num,
        .name=name,
    };
    Table$set(&function_info, &fn, &info, Table$info(Function$info("???"), &func_info_type));
}

PUREFUNC static func_info_t *get_function_info(void *fn)
{
    func_info_t *info = Table$get(function_info, &fn, Table$info(Function$info("???"), &func_info_type));
    if (info) return info;

    void *closest_fn = NULL;
    for (int64_t i = 0; i < function_info.entries.length; i++) {
        struct { void *fn; func_info_t info; } *entry = function_info.entries.data + i*function_info.entries.stride;
        if (entry->fn > fn || entry->fn < closest_fn) continue;
        closest_fn = entry->fn;
        info = &entry->info;
    }
    return info;
}

PUREFUNC public OptionalText_t get_function_name(void *fn)
{
    func_info_t *info = get_function_info(fn);
    return info ? info->name : NONE_TEXT;
}

PUREFUNC public OptionalText_t get_function_filename(void *fn)
{
    func_info_t *info = get_function_info(fn);
    return info ? info->filename : NONE_TEXT;
}

PUREFUNC public int64_t get_function_line_num(void *fn)
{
    func_info_t *info = get_function_info(fn);
    return info ? info->line_num : -1;
}

public Text_t Func$as_text(const void *fn, bool colorize, const TypeInfo_t *type)
{
    (void)fn;
    Text_t text = Text$from_str(type->FunctionInfo.type_str);
    if (fn) {
        OptionalText_t name = get_function_name(*(void**)fn);
        if (name.length >= 0)
            text = name;

        OptionalText_t filename = get_function_filename(*(void**)fn);
        int64_t line_num = get_function_line_num(*(void**)fn);
        if (filename.length >= 0)
            text = Text$format("%k [%k:%ld]", &text, &filename, line_num);
    }
    if (fn && colorize)
        text = Text$concat(Text("\x1b[32;1m"), text, Text("\x1b[m"));
    return text;
}

public PUREFUNC bool Func$is_none(const void *obj, const TypeInfo_t*)
{
    return *(void**)obj == NULL;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
