// This file defines how to compile enums

#include "../ast.h"
#include "../environment.h"
#include "../naming.h"
#include "../stdlib/tables.h"
#include "../stdlib/text.h"
#include "../typecheck.h"
#include "compilation.h"

Text_t compile_enum_typeinfo(env_t *env, ast_t *ast) {
    DeclareMatch(def, ast, EnumDef);

    // Compile member types and constructors:
    Text_t member_typeinfos = EMPTY_TEXT;
    for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
        if (!tag->fields) continue;

        const char *tag_name = String(def->name, "$", tag->name);
        type_t *tag_type = Table$str_get(*env->types, tag_name);
        assert(tag_type && tag_type->tag == StructType);
        member_typeinfos =
            Texts(member_typeinfos, compile_struct_typeinfo(env, tag_type, tag_name, tag->fields, tag->secret, false));
    }

    int num_tags = 0;
    for (tag_ast_t *t = def->tags; t; t = t->next)
        num_tags += 1;

    type_t *t = Table$str_get(*env->types, def->name);
    const char *metamethods = is_packed_data(t) ? "PackedDataEnum$metamethods" : "Enum$metamethods";
    Text_t info = namespace_name(env, env->namespace, Texts(def->name, "$$info"));
    Text_t typeinfo =
        Texts("public const TypeInfo_t ", info, " = {", (int64_t)type_size(t), "u, ", (int64_t)type_align(t),
              "u, .metamethods=", metamethods, ", {.tag=EnumInfo, .EnumInfo={.name=\"", def->name,
              "\", "
              ".num_tags=",
              (int64_t)num_tags, ", .tags=(NamedType_t[]){");

    for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
        const char *tag_type_name = String(def->name, "$", tag->name);
        type_t *tag_type = Table$str_get(*env->types, tag_type_name);
        if (tag_type && Match(tag_type, StructType)->fields)
            typeinfo = Texts(typeinfo, "{\"", tag->name, "\", ", compile_type_info(tag_type), "}, ");
        else typeinfo = Texts(typeinfo, "{\"", tag->name, "\"}, ");
    }
    typeinfo = Texts(typeinfo, "}}}};\n");
    return Texts(member_typeinfos, typeinfo);
}

Text_t compile_enum_constructors(env_t *env, ast_t *ast) {
    DeclareMatch(def, ast, EnumDef);
    Text_t constructors = EMPTY_TEXT;
    for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
        if (!tag->fields) continue;

        Text_t arg_sig = EMPTY_TEXT;
        for (arg_ast_t *field = tag->fields; field; field = field->next) {
            type_t *field_t = get_arg_ast_type(env, field);
            arg_sig = Texts(arg_sig, compile_declaration(field_t, Texts("$", field->name)));
            if (field->next) arg_sig = Texts(arg_sig, ", ");
        }
        if (arg_sig.length == 0) arg_sig = Text("void");
        Text_t type_name = namespace_name(env, env->namespace, Texts(def->name, "$$type"));
        Text_t tagged_name = namespace_name(env, env->namespace, Texts(def->name, "$tagged$", tag->name));
        Text_t tag_name = namespace_name(env, env->namespace, Texts(def->name, "$tag$", tag->name));
        Text_t constructor_impl = Texts("public inline ", type_name, " ", tagged_name, "(", arg_sig, ") { return (",
                                        type_name, "){.$tag=", tag_name, ", .", valid_c_name(tag->name), "={");
        for (arg_ast_t *field = tag->fields; field; field = field->next) {
            constructor_impl = Texts(constructor_impl, "$", field->name);
            if (field->next) constructor_impl = Texts(constructor_impl, ", ");
        }
        constructor_impl = Texts(constructor_impl, "}}; }\n");
        constructors = Texts(constructors, constructor_impl);
    }
    return constructors;
}

Text_t compile_enum_header(env_t *env, ast_t *ast) {
    DeclareMatch(def, ast, EnumDef);
    Text_t all_defs = EMPTY_TEXT;
    Text_t none_name = namespace_name(env, env->namespace, Texts(def->name, "$none"));
    Text_t enum_name = namespace_name(env, env->namespace, Texts(def->name, "$$enum"));
    Text_t enum_tags = Texts("{ ", none_name, "=0, ");

    bool has_any_tags_with_fields = false;
    for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
        Text_t tag_name = namespace_name(env, env->namespace, Texts(def->name, "$tag$", tag->name));
        enum_tags = Texts(enum_tags, tag_name);
        if (tag->next) enum_tags = Texts(enum_tags, ", ");
        has_any_tags_with_fields = has_any_tags_with_fields || (tag->fields != NULL);
    }
    enum_tags = Texts(enum_tags, " }");

    if (!has_any_tags_with_fields) {
        Text_t enum_def = Texts("enum ", enum_name, " ", enum_tags, ";\n");
        Text_t info = namespace_name(env, env->namespace, Texts(def->name, "$$info"));
        return Texts(enum_def, "extern const TypeInfo_t ", info, ";\n");
    }

    Text_t struct_name = namespace_name(env, env->namespace, Texts(def->name, "$$struct"));
    Text_t enum_def = Texts("struct ", struct_name,
                            " {\n"
                            "enum ",
                            enum_tags,
                            " $tag;\n"
                            "union {\n");
    for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
        if (!tag->fields) continue;
        Text_t field_def = compile_struct_header(
            env,
            WrapAST(ast, StructDef, .name = Text$as_c_string(Texts(def->name, "$", tag->name)), .fields = tag->fields));
        all_defs = Texts(all_defs, field_def);
        Text_t tag_type = namespace_name(env, env->namespace, Texts(def->name, "$", tag->name, "$$type"));
        enum_def = Texts(enum_def, tag_type, " ", valid_c_name(tag->name), ";\n");
    }
    enum_def = Texts(enum_def, "};\n};\n");
    all_defs = Texts(all_defs, enum_def);

    Text_t info = namespace_name(env, env->namespace, Texts(def->name, "$$info"));
    all_defs = Texts(all_defs, "extern const TypeInfo_t ", info, ";\n");
    for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
        if (!tag->fields) continue;

        Text_t arg_sig = EMPTY_TEXT;
        for (arg_ast_t *field = tag->fields; field; field = field->next) {
            type_t *field_t = get_arg_ast_type(env, field);
            arg_sig = Texts(arg_sig, compile_declaration(field_t, Texts("$", field->name)));
            if (field->next) arg_sig = Texts(arg_sig, ", ");
        }
        if (arg_sig.length == 0) arg_sig = Text("void");
        Text_t enum_type = namespace_name(env, env->namespace, Texts(def->name, "$$type"));
        Text_t tagged_name = namespace_name(env, env->namespace, Texts(def->name, "$tagged$", tag->name));
        Text_t constructor_def = Texts(enum_type, " ", tagged_name, "(", arg_sig, ");\n");
        all_defs = Texts(all_defs, constructor_def);
    }
    return all_defs;
}

public
Text_t compile_empty_enum(type_t *t) {
    DeclareMatch(enum_, t, EnumType);
    tag_t *tag = enum_->tags;
    assert(tag);
    assert(tag->type);
    if (Match(tag->type, StructType)->fields)
        return Texts("((", compile_type(t), "){.$tag=", tag->tag_value, ", .", tag->name, "=", compile_empty(tag->type),
                     "})");
    else if (enum_has_fields(t)) return Texts("((", compile_type(t), "){.$tag=", tag->tag_value, "})");
    else return Texts("((", compile_type(t), ")", tag->tag_value, ")");
}

public
Text_t compile_enum_field_access(env_t *env, ast_t *ast) {
    DeclareMatch(f, ast, FieldAccess);
    type_t *fielded_t = get_type(env, f->fielded);
    type_t *value_t = value_type(fielded_t);
    DeclareMatch(e, value_t, EnumType);
    for (tag_t *tag = e->tags; tag; tag = tag->next) {
        if (streq(f->field, tag->name)) {
            Text_t tag_name = namespace_name(e->env, e->env->namespace, Texts("tag$", tag->name));
            if (fielded_t->tag == PointerType) {
                Text_t fielded = compile_to_pointer_depth(env, f->fielded, 1, false);
                return Texts("((", fielded, ")->$tag == ", tag_name, ")");
            } else if (enum_has_fields(value_t)) {
                Text_t fielded = compile(env, f->fielded);
                return Texts("((", fielded, ").$tag == ", tag_name, ")");
            } else {
                Text_t fielded = compile(env, f->fielded);
                return Texts("((", fielded, ") == ", tag_name, ")");
            }
        }
    }
    code_err(ast, "The field '", f->field, "' is not a valid tag name of ", type_to_str(value_t));
}
