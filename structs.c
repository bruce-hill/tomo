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

void compile_struct_def(env_t *env, ast_t *ast)
{
    auto def = Match(ast, StructDef);
    CORD full_name = CORD_cat(namespace_prefix(env, env->namespace), def->name);

    type_t *t = Table$str_get(*env->types, def->name);
    assert(t && t->tag == StructType);
    int num_fields = 0;
    for (arg_ast_t *f = def->fields; f; f = f->next)
        num_fields += 1;
    const char *short_name = def->name;
    if (strchr(short_name, '$'))
        short_name = strrchr(short_name, '$') + 1;

    const char *metamethods = is_packed_data(t) ? "PackedData$metamethods" : "Struct$metamethods";
    CORD typeinfo = CORD_asprintf("public const TypeInfo_t %r = {.size=%zu, .align=%zu, .metamethods=%s, "
                                  ".tag=StructInfo, .StructInfo.name=\"%s\"%s, "
                                  ".StructInfo.num_fields=%ld",
                                  full_name, type_size(t), type_align(t), metamethods, short_name, def->secret ? ", .StructInfo.is_secret=true" : "",
                                  num_fields);
    if (def->fields) {
        typeinfo = CORD_asprintf("%r, .StructInfo.fields=(NamedType_t[%d]){", typeinfo, num_fields);
        for (arg_ast_t *f = def->fields; f; f = f->next) {
            type_t *field_type = get_arg_ast_type(env, f);
            typeinfo = CORD_all(typeinfo, "{\"", f->name, "\", ", compile_type_info(env, field_type), "}");
            if (f->next) typeinfo = CORD_all(typeinfo, ", ");
        }
        typeinfo = CORD_all(typeinfo, "}");
    }
    typeinfo = CORD_all(typeinfo, "};\n");
    env->code->typeinfos = CORD_all(env->code->typeinfos, typeinfo);

    compile_namespace(env, def->name, def->namespace);
}

CORD compile_struct_header(env_t *env, ast_t *ast)
{
    auto def = Match(ast, StructDef);
    CORD full_name = CORD_cat(namespace_prefix(env, env->namespace), def->name);

    CORD fields = CORD_EMPTY;
    for (arg_ast_t *field = def->fields; field; field = field->next) {
        type_t *field_t = get_arg_ast_type(env, field);
        CORD type_code = compile_type(field_t);
        fields = CORD_all(fields, type_code, " $", field->name, field_t->tag == BoolType ? ":1" : CORD_EMPTY, ";\n");
    }
    CORD struct_code = CORD_all("struct ", full_name, "_s {\n");
    struct_code = CORD_all(struct_code, "};\n");
    type_t *t = Table$str_get(*env->types, def->name);
    return CORD_all(
        "typedef struct ", full_name, "_s ", full_name, "_t;\n",
        "struct ", full_name, "_s {\n",
        fields,
        "};\n",
        "typedef struct {\n",
        "union {\n",
        full_name, "_t value;\n"
        "struct {\n"
        "char _padding[", heap_strf("%zu", unpadded_struct_size(t)), "];\n",
        "Bool_t is_none:1;\n"
        "};\n"
        "};\n"
        "} ", namespace_prefix(env, env->namespace), "$Optional", def->name, "_t;\n"
        "extern const TypeInfo_t ", full_name, ";\n");
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
