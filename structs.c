// Logic for compiling new struct types defined in code
#include <ctype.h>
#include <gc/cord.h>
#include <gc.h>
#include <stdio.h>

#include "ast.h"
#include "stdlib/text.h"
#include "compile.h"
#include "cordhelpers.h"
#include "environment.h"
#include "typecheck.h"
#include "stdlib/util.h"

CORD compile_struct_typeinfo(env_t *env, type_t *t, const char *name, arg_ast_t *fields, bool is_secret)
{
    CORD full_name = CORD_cat(namespace_prefix(env, env->namespace), name);

    int num_fields = 0;
    for (arg_ast_t *f = fields; f; f = f->next)
        num_fields += 1;
    const char *short_name = name;
    if (strchr(short_name, '$'))
        short_name = strrchr(short_name, '$') + 1;

    const char *metamethods = is_packed_data(t) ? "PackedData$metamethods" : "Struct$metamethods";
    CORD typeinfo = CORD_asprintf("public const TypeInfo_t %r$$info = {.size=%zu, .align=%zu, .metamethods=%s, "
                                  ".tag=StructInfo, .StructInfo.name=\"%s\"%s, "
                                  ".StructInfo.num_fields=%ld",
                                  full_name, type_size(t), type_align(t), metamethods, short_name, is_secret ? ", .StructInfo.is_secret=true" : "",
                                  num_fields);
    if (fields) {
        typeinfo = CORD_asprintf("%r, .StructInfo.fields=(NamedType_t[%d]){", typeinfo, num_fields);
        for (arg_ast_t *f = fields; f; f = f->next) {
            type_t *field_type = get_arg_ast_type(env, f);
            typeinfo = CORD_all(typeinfo, "{\"", f->name, "\", ", compile_type_info(field_type), "}");
            if (f->next) typeinfo = CORD_all(typeinfo, ", ");
        }
        typeinfo = CORD_all(typeinfo, "}");
    }
    return CORD_all(typeinfo, "};\n");
}

CORD compile_struct_header(env_t *env, ast_t *ast)
{
    auto def = Match(ast, StructDef);
    CORD full_name = CORD_cat(namespace_prefix(env, env->namespace), def->name);

    CORD fields = CORD_EMPTY;
    for (arg_ast_t *field = def->fields; field; field = field->next) {
        type_t *field_t = get_arg_ast_type(env, field);
        fields = CORD_all(fields, compile_declaration(field_t, field->name), field_t->tag == BoolType ? ":1" : CORD_EMPTY, ";\n");
    }
    CORD struct_code = def->external ? CORD_EMPTY : CORD_all("struct ", full_name, "$$struct {\n", fields, "};\n");
    type_t *t = Table$str_get(*env->types, def->name);
    return CORD_all(
        struct_code,
        "DEFINE_OPTIONAL_TYPE(", compile_type(t), ", ", heap_strf("%zu", unpadded_struct_size(t)),
            ", ", namespace_prefix(env, env->namespace), "$Optional", def->name, "$$type);\n"
        "extern const TypeInfo_t ", full_name, "$$info;\n");
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
