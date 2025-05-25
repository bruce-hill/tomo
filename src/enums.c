// Logic for compiling tagged unions (enums)
#include <ctype.h>
#include <gc/cord.h>
#include <gc.h>
#include <stdio.h>

#include "ast.h"
#include "stdlib/text.h"
#include "compile.h"
#include "cordhelpers.h"
#include "structs.h"
#include "environment.h"
#include "typecheck.h"
#include "stdlib/util.h"

CORD compile_enum_typeinfo(env_t *env, ast_t *ast)
{
    DeclareMatch(def, ast, EnumDef);

    // Compile member types and constructors:
    CORD member_typeinfos = CORD_EMPTY;
    for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
        if (!tag->fields) continue;

        const char *tag_name = String(def->name, "$", tag->name);
        type_t *tag_type = Table$str_get(*env->types, tag_name);
        assert(tag_type && tag_type->tag == StructType);
        member_typeinfos = CORD_all(
            member_typeinfos, compile_struct_typeinfo(env, tag_type, tag_name, tag->fields, tag->secret, false));
    }

    int num_tags = 0;
    for (tag_ast_t *t = def->tags; t; t = t->next)
        num_tags += 1;

    type_t *t = Table$str_get(*env->types, def->name);
    const char *metamethods = is_packed_data(t) ? "PackedDataEnum$metamethods" : "Enum$metamethods";
    CORD info = namespace_name(env, env->namespace, CORD_all(def->name, "$$info"));
    CORD typeinfo = CORD_all("public const TypeInfo_t ", info, " = {",
                             String((int64_t)type_size(t)), "u, ", String((int64_t)type_align(t)), "u, .metamethods=", metamethods,
                             ", {.tag=EnumInfo, .EnumInfo={.name=\"", def->name, "\", "
                             ".num_tags=", String((int64_t)num_tags), ", .tags=(NamedType_t[]){");

    for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
        const char *tag_type_name = String(def->name, "$", tag->name);
        type_t *tag_type = Table$str_get(*env->types, tag_type_name);
        if (tag_type && Match(tag_type, StructType)->fields)
            typeinfo = CORD_all(typeinfo, "{\"", tag->name, "\", ", compile_type_info(tag_type), "}, ");
        else
            typeinfo = CORD_all(typeinfo, "{\"", tag->name, "\"}, ");
    }
    typeinfo = CORD_all(typeinfo, "}}}};\n");
    return CORD_all(member_typeinfos, typeinfo);
}

CORD compile_enum_constructors(env_t *env, ast_t *ast)
{
    DeclareMatch(def, ast, EnumDef);
    CORD constructors = CORD_EMPTY;
    for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
        if (!tag->fields) continue;

        CORD arg_sig = CORD_EMPTY;
        for (arg_ast_t *field = tag->fields; field; field = field->next) {
            type_t *field_t = get_arg_ast_type(env, field);
            arg_sig = CORD_all(arg_sig, compile_declaration(field_t, CORD_all("$", field->name)));
            if (field->next) arg_sig = CORD_cat(arg_sig, ", ");
        }
        if (arg_sig == CORD_EMPTY) arg_sig = "void";
        CORD type_name = namespace_name(env, env->namespace, CORD_all(def->name, "$$type"));
        CORD tagged_name = namespace_name(env, env->namespace, CORD_all(def->name, "$tagged$", tag->name));
        CORD tag_name = namespace_name(env, env->namespace, CORD_all(def->name, "$tag$", tag->name));
        CORD constructor_impl = CORD_all("public inline ", type_name, " ", tagged_name, "(", arg_sig, ") { return (",
                                         type_name, "){.$tag=", tag_name, ", .", tag->name, "={");
        for (arg_ast_t *field = tag->fields; field; field = field->next) {
            constructor_impl = CORD_all(constructor_impl, "$", field->name);
            if (field->next) constructor_impl = CORD_cat(constructor_impl, ", ");
        }
        constructor_impl = CORD_cat(constructor_impl, "}}; }\n");
        constructors = CORD_cat(constructors, constructor_impl);
    }
    return constructors;
}

CORD compile_enum_header(env_t *env, ast_t *ast)
{
    DeclareMatch(def, ast, EnumDef);
    CORD all_defs = CORD_EMPTY;
    CORD none_name = namespace_name(env, env->namespace, CORD_all(def->name, "$none"));
    CORD enum_name = namespace_name(env, env->namespace, CORD_all(def->name, "$$enum"));
    CORD enum_tags = CORD_all("{ ", none_name, "=0, ");

    bool has_any_tags_with_fields = false;
    for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
        CORD tag_name = namespace_name(env, env->namespace, CORD_all(def->name, "$tag$", tag->name));
        enum_tags = CORD_all(enum_tags, tag_name);
        if (tag->next) enum_tags = CORD_all(enum_tags, ", ");
        has_any_tags_with_fields = has_any_tags_with_fields || (tag->fields != NULL);
    }
    enum_tags = CORD_all(enum_tags, " }");

    if (!has_any_tags_with_fields) {
        CORD enum_def = CORD_all("enum ", enum_name, " ", enum_tags, ";\n");
        CORD info = namespace_name(env, env->namespace, CORD_all(def->name, "$$info"));
        return CORD_all(enum_def, "extern const TypeInfo_t ", info, ";\n");
    }

    CORD struct_name = namespace_name(env, env->namespace, CORD_all(def->name, "$$struct"));
    CORD enum_def = CORD_all("struct ", struct_name, " {\n"
                             "enum ", enum_tags, " $tag;\n"
                             "union {\n");
    for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
        if (!tag->fields) continue;
        CORD field_def = compile_struct_header(env, WrapAST(ast, StructDef, .name=CORD_to_const_char_star(CORD_all(def->name, "$", tag->name)), .fields=tag->fields));
        all_defs = CORD_all(all_defs, field_def);
        CORD tag_type = namespace_name(env, env->namespace, CORD_all(def->name, "$", tag->name, "$$type"));
        enum_def = CORD_all(enum_def, tag_type, " ", tag->name, ";\n");
    }
    enum_def = CORD_all(enum_def, "};\n};\n");
    all_defs = CORD_all(all_defs, enum_def);

    CORD info = namespace_name(env, env->namespace, CORD_all(def->name, "$$info"));
    all_defs = CORD_all(all_defs, "extern const TypeInfo_t ", info, ";\n");
    for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
        if (!tag->fields) continue;

        CORD arg_sig = CORD_EMPTY;
        for (arg_ast_t *field = tag->fields; field; field = field->next) {
            type_t *field_t = get_arg_ast_type(env, field);
            arg_sig = CORD_all(arg_sig, compile_declaration(field_t, CORD_all("$", field->name)));
            if (field->next) arg_sig = CORD_all(arg_sig, ", ");
        }
        if (arg_sig == CORD_EMPTY) arg_sig = "void";
        CORD enum_type = namespace_name(env, env->namespace, CORD_all(def->name, "$$type"));
        CORD tagged_name = namespace_name(env, env->namespace, CORD_all(def->name, "$tagged$", tag->name));
        CORD constructor_def = CORD_all(enum_type, " ", tagged_name, "(", arg_sig, ");\n");
        all_defs = CORD_all(all_defs, constructor_def);
    }
    return all_defs;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
