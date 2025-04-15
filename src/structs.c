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

CORD compile_struct_typeinfo(env_t *env, type_t *t, const char *name, arg_ast_t *fields, bool is_secret, bool is_opaque)
{
    CORD typeinfo_name = CORD_all(namespace_prefix(env, env->namespace), name, "$$info");
    CORD type_code = Match(t, StructType)->external ? name : CORD_all("struct ", namespace_prefix(env, env->namespace), name, "$$struct");

    int num_fields = 0;
    for (arg_ast_t *f = fields; f; f = f->next)
        num_fields += 1;
    const char *short_name = name;
    if (strchr(short_name, '$'))
        short_name = strrchr(short_name, '$') + 1;

    const char *metamethods = is_packed_data(t) ? "PackedData$metamethods" : "Struct$metamethods";
    CORD typeinfo = CORD_asprintf("public const TypeInfo_t %r = {.size=sizeof(%r), .align=__alignof__(%r), .metamethods=%s, "
                                  ".tag=StructInfo, .StructInfo.name=\"%s\"%s%s, "
                                  ".StructInfo.num_fields=%ld",
                                  typeinfo_name, type_code, type_code, metamethods, short_name, is_secret ? ", .StructInfo.is_secret=true" : "",
                                  is_opaque ? ", .StructInfo.is_opaque=true" : "",
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
    DeclareMatch(def, ast, StructDef);
    CORD typeinfo_name = CORD_all(namespace_prefix(env, env->namespace), def->name, "$$info");
    CORD type_code = def->external ? def->name : CORD_all("struct ", namespace_prefix(env, env->namespace), def->name, "$$struct");

    CORD fields = CORD_EMPTY;
    for (arg_ast_t *field = def->fields; field; field = field->next) {
        type_t *field_t = get_arg_ast_type(env, field);
        type_t *check_for_opaque = non_optional(field_t);
        if (check_for_opaque->tag == StructType && Match(check_for_opaque, StructType)->opaque) {
            if (field->type)
                code_err(field->type, "This is an opaque type, so it can't be used as a struct field type");
            else if (field->value)
                code_err(field->value, "This is an opaque type, so it can't be used as a struct field type");
        }
        fields = CORD_all(fields, compile_declaration(field_t, field->name), field_t->tag == BoolType ? ":1" : CORD_EMPTY, ";\n");
    }
    CORD struct_code = def->external ? CORD_EMPTY : CORD_all(type_code, " {\n", fields, "};\n");
    type_t *t = Table$str_get(*env->types, def->name);

    CORD unpadded_size = def->opaque ? CORD_all("sizeof(", type_code, ")") : CORD_asprintf("%zu", unpadded_struct_size(t));
    CORD typeinfo_code = CORD_all("extern const TypeInfo_t ", typeinfo_name, ";\n");
    CORD optional_code = CORD_EMPTY;
    if (!def->opaque) {
        optional_code = CORD_all("DEFINE_OPTIONAL_TYPE(", compile_type(t), ", ", unpadded_size, ",",
                                 namespace_prefix(env, env->namespace), "$Optional", def->name, "$$type);\n");
    }
    return CORD_all(struct_code, optional_code, typeinfo_code);
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
