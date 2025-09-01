// This file defines how to compile structs

#include <gc.h>

#include "../ast.h"
#include "../environment.h"
#include "../naming.h"
#include "../stdlib/tables.h"
#include "../stdlib/text.h"
#include "../typecheck.h"
#include "compilation.h"

public
Text_t compile_struct_typeinfo(env_t *env, type_t *t, const char *name, arg_ast_t *fields, bool is_secret,
                               bool is_opaque) {
    Text_t typeinfo_name = namespace_name(env, env->namespace, Texts(name, "$$info"));
    Text_t type_code = Match(t, StructType)->external
                           ? Text$from_str(name)
                           : Texts("struct ", namespace_name(env, env->namespace, Texts(name, "$$struct")));

    int64_t num_fields = 0;
    for (arg_ast_t *f = fields; f; f = f->next)
        num_fields += 1;
    const char *short_name = name;
    if (strchr(short_name, '$')) short_name = strrchr(short_name, '$') + 1;

    const char *metamethods = is_packed_data(t) ? "PackedData$metamethods" : "Struct$metamethods";
    Text_t typeinfo = Texts(
        "public const TypeInfo_t ", typeinfo_name, " = {.size=sizeof(", type_code, "), .align=__alignof__(", type_code,
        "), "
        ".metamethods=",
        metamethods,
        ", "
        ".tag=StructInfo, .StructInfo.name=\"",
        short_name, "\"", is_secret ? Text(", .StructInfo.is_secret=true") : EMPTY_TEXT,
        is_opaque ? Text(", .StructInfo.is_opaque=true") : EMPTY_TEXT, ", .StructInfo.num_fields=", num_fields);
    if (fields) {
        typeinfo = Texts(typeinfo, ", .StructInfo.fields=(NamedType_t[", num_fields, "]){");
        for (arg_ast_t *f = fields; f; f = f->next) {
            type_t *field_type = get_arg_ast_type(env, f);
            typeinfo = Texts(typeinfo, "{\"", f->name, "\", ", compile_type_info(field_type), "}");
            if (f->next) typeinfo = Texts(typeinfo, ", ");
        }
        typeinfo = Texts(typeinfo, "}");
    }
    return Texts(typeinfo, "};\n");
}

public
Text_t compile_struct_header(env_t *env, ast_t *ast) {
    DeclareMatch(def, ast, StructDef);
    Text_t typeinfo_name = namespace_name(env, env->namespace, Texts(def->name, "$$info"));
    Text_t type_code = def->external
                           ? Text$from_str(def->name)
                           : Texts("struct ", namespace_name(env, env->namespace, Texts(def->name, "$$struct")));

    Text_t fields = EMPTY_TEXT;
    for (arg_ast_t *field = def->fields; field; field = field->next) {
        type_t *field_t = get_arg_ast_type(env, field);
        type_t *check_for_opaque = non_optional(field_t);
        if (check_for_opaque->tag == StructType && Match(check_for_opaque, StructType)->opaque) {
            if (field->type)
                code_err(field->type, "This is an opaque type, so it can't be used as a struct field type");
            else if (field->value)
                code_err(field->value, "This is an opaque type, so it can't be used as a struct field type");
        }
        fields = Texts(fields, compile_declaration(field_t, valid_c_name(field->name)),
                       field_t->tag == BoolType ? Text(":1") : EMPTY_TEXT, ";\n");
    }
    Text_t struct_code = def->external ? EMPTY_TEXT : Texts(type_code, " {\n", fields, "};\n");
    type_t *t = Table$str_get(*env->types, def->name);

    Text_t unpadded_size = def->opaque ? Texts("sizeof(", type_code, ")") : Texts((int64_t)unpadded_struct_size(t));
    Text_t typeinfo_code = Texts("extern const TypeInfo_t ", typeinfo_name, ";\n");
    Text_t optional_code = EMPTY_TEXT;
    if (!def->opaque) {
        optional_code = Texts("DEFINE_OPTIONAL_TYPE(", compile_type(t), ", ", unpadded_size, ", ",
                              namespace_name(env, env->namespace, Texts("$Optional", def->name, "$$type")), ");\n");
    }
    return Texts(struct_code, optional_code, typeinfo_code);
}

public
Text_t compile_empty_struct(type_t *t) {
    DeclareMatch(struct_, t, StructType);
    Text_t code = Texts("((", compile_type(t), "){");
    for (arg_t *field = struct_->fields; field; field = field->next) {
        Text_t empty_field =
            field->default_val ? compile(struct_->env, field->default_val) : compile_empty(field->type);
        if (empty_field.length == 0) return EMPTY_TEXT;

        code = Texts(code, empty_field);
        if (field->next) code = Texts(code, ", ");
    }
    return Texts(code, "})");
}

public
Text_t compile_struct_field_access(env_t *env, ast_t *ast) {
    DeclareMatch(f, ast, FieldAccess);
    type_t *fielded_t = get_type(env, f->fielded);
    type_t *value_t = value_type(fielded_t);
    for (arg_t *field = Match(value_t, StructType)->fields; field; field = field->next) {
        if (streq(field->name, f->field)) {
            if (fielded_t->tag == PointerType) {
                Text_t fielded = compile_to_pointer_depth(env, f->fielded, 1, false);
                return Texts("(", fielded, ")->", valid_c_name(f->field));
            } else {
                Text_t fielded = compile(env, f->fielded);
                return Texts("(", fielded, ").", valid_c_name(f->field));
            }
        }
    }
    code_err(ast, "The field '", f->field, "' is not a valid field name of ", type_to_str(value_t));
}

public
Text_t compile_struct_literal(env_t *env, ast_t *ast, type_t *t, arg_ast_t *args) {
    DeclareMatch(struct_, t, StructType);
    if (struct_->opaque) code_err(ast, "This struct is opaque, so I don't know what's inside it!");

    call_opts_t constructor_opts = {
        .promotion = true,
        .underscores = (env->current_type != NULL && type_eq(env->current_type, t)),
    };
    if (is_valid_call(env, struct_->fields, args, constructor_opts)) {
        return Texts("((", compile_type(t), "){", compile_arguments(env, ast, struct_->fields, args), "})");
    } else if (!constructor_opts.underscores
               && is_valid_call(env, struct_->fields, args, (call_opts_t){.promotion = true, .underscores = true})) {
        code_err(ast, "This constructor uses private fields that are not exposed.");
    }
    code_err(ast, "I could not find a constructor matching these arguments for the struct ", type_to_str(t));
}
